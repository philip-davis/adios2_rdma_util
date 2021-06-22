
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include<mpi.h>

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_rma.h>

#ifdef SST_HAVE_FI_GNI
#include <rdma/fi_ext_gni.h>
#ifdef SST_HAVE_CRAY_DRC
#include <rdmacred.h>

#define DP_DRC_MAX_TRY 60
#define DP_DRC_WAIT_USEC 1000000

#endif /* SST_HAVE_CRAY_DRC */
#endif /* SST_HAVE_FI_GNI */

#define DP_AV_DEF_SIZE 512

struct fabric_state
{
    struct fi_context *ctx;
    struct fi_info *info;
    int local_mr_req;
    int rx_cq_data;
    size_t addr_len;
    size_t msg_prefix_size;
    struct fid_fabric *fabric;
    struct fid_domain *domain;
    struct fid_ep *signal;
    struct fid_cq *cq_signal;
    struct fid_av *av;
    pthread_t listener;
#ifdef SST_HAVE_CRAY_DRC
    drc_info_handle_t drc_info;
    uint32_t credential;
    struct fi_gni_auth_key *auth_key;
#endif /* SST_HAVE_CRAY_DRC */
};

static int init_fabric(struct fabric_state *fabric, const char *ifname)
{
    struct fi_info *hints, *info, *originfo, *useinfo;
    struct fi_av_attr av_attr = {0};
    struct fi_cq_attr cq_attr = {0};
    int result;

    hints = fi_allocinfo();
    hints->caps = FI_MSG | FI_SEND | FI_RECV | FI_REMOTE_READ |
                  FI_REMOTE_WRITE | FI_RMA | FI_READ | FI_WRITE;
    hints->mode = FI_CONTEXT | FI_LOCAL_MR | FI_CONTEXT2 | FI_MSG_PREFIX |
                  FI_ASYNC_IOV | FI_RX_CQ_DATA;
    hints->domain_attr->mr_mode = FI_MR_BASIC;
    hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
    hints->domain_attr->data_progress = FI_PROGRESS_AUTO;
    hints->ep_attr->type = FI_EP_RDM;

    fabric->info = NULL;

    fprintf(stderr, "INFO: initialzing fabric...\n");

    fi_getinfo(FI_VERSION(1, 5), NULL, NULL, 0, hints, &info);
    if (!info)
    {
        fprintf(stderr, "ERROR: no fabrics detected.\n");
        fabric->info = NULL;
        return -1;
    }
    fi_freeinfo(hints);

    originfo = info;
    useinfo = NULL;
    while (info)
    {
        char *prov_name = info->fabric_attr->prov_name;
        char *domain_name = info->domain_attr->name;

        if (ifname && strcmp(ifname, domain_name) == 0)
        {
            fprintf(stderr, "INFO: using specified interface '%s'.\n", ifname);
            useinfo = info;
            break;
        }
        if ((((strcmp(prov_name, "verbs") == 0) && info->src_addr) ||
             (strcmp(prov_name, "gni") == 0) ||
             (strcmp(prov_name, "psm2") == 0)) &&
            (!useinfo || !ifname ||
             (strcmp(useinfo->domain_attr->name, ifname) != 0)))
        {
            fprintf(stderr, "INFO: seeing candidate fabric %s, will use this unless we see something better.\n", prov_name);
            useinfo = info;
        }
        else if (((strstr(prov_name, "verbs") && info->src_addr) ||
                  strstr(prov_name, "gni") || strstr(prov_name, "psm2")) &&
                 !useinfo)
        {
            fprintf(stderr, "INFO: seeing candidate fabric %s, will use this unless we see something better.\n", prov_name);
            useinfo = info;
        }
        else
        {
                fprintf(stderr, "ignoring fabric %s because it's not of a supported type.\n", prov_name);
        }
        info = info->next;
    }

    info = useinfo;

    if (!info)
    {
        fprintf(stderr, "ERROR: "
            "none of the usable system fabrics are supported high speed "
            "interfaces (verbs, gni, psm2.) To use a compatible fabric that is "
            "being ignored (probably sockets), set the environment variable "
            "FABRIC_IFACE to the interface name. Check the output of fi_info "
            "to troubleshoot this message.\n");
        fabric->info = NULL;
        return -1;
    }

    if (info->mode & FI_CONTEXT2)
    {
        fabric->ctx = calloc(2, sizeof(*fabric->ctx));
    }
    else if (info->mode & FI_CONTEXT)
    {
        fabric->ctx = calloc(1, sizeof(*fabric->ctx));
    }
    else
    {
        fabric->ctx = NULL;
    }

    if (info->mode & FI_LOCAL_MR)
    {
        fabric->local_mr_req = 1;
    }
    else
    {
        fabric->local_mr_req = 0;
    }

    if (info->mode & FI_MSG_PREFIX)
    {
        fabric->msg_prefix_size = info->ep_attr->msg_prefix_size;
    }
    else
    {
        fabric->msg_prefix_size = 0;
    }

    if (info->mode & FI_RX_CQ_DATA)
    {
        fabric->rx_cq_data = 1;
    }
    else
    {
        fabric->rx_cq_data = 0;
    }

    fabric->addr_len = info->src_addrlen;

    info->domain_attr->mr_mode = FI_MR_BASIC;
#ifdef SST_HAVE_CRAY_DRC
    if (strstr(info->fabric_attr->prov_name, "gni") && fabric->auth_key)
    {
        info->domain_attr->auth_key = (uint8_t *)fabric->auth_key;
        info->domain_attr->auth_key_size = sizeof(struct fi_gni_raw_auth_key);
    }
#endif /* SST_HAVE_CRAY_DRC */
    fabric->info = fi_dupinfo(info);
    if (!fabric->info)
    {
        fprintf(stderr, 
                      "ERROR: copying the fabric info failed.\n");
        return -1;
    }

        fprintf(stderr,          
        "INFO: fabric parameters to use at fabric initialization: %s\n",
                  fi_tostr(fabric->info, FI_TYPE_INFO));

    result = fi_fabric(info->fabric_attr, &fabric->fabric, fabric->ctx);
    if (result != FI_SUCCESS)
    {
        fprintf(stderr, 
            "ERROR: opening fabric access failed with %d (%s). This is fatal.\n",
            result, fi_strerror(result));
        return -1;
    }
    result = fi_domain(fabric->fabric, info, &fabric->domain, fabric->ctx);
    if (result != FI_SUCCESS)
    {
       fprintf(stderr,                
            "ERROR: accessing domain failed with %d (%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }
    info->ep_attr->type = FI_EP_RDM;
    result = fi_endpoint(fabric->domain, info, &fabric->signal, fabric->ctx);
    if (result != FI_SUCCESS || !fabric->signal)
    {
        fprintf(stderr,               
            "ERROR: opening endpoint failed with %d (%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }

    av_attr.type = FI_AV_MAP;
    av_attr.count = DP_AV_DEF_SIZE;
    av_attr.ep_per_node = 0;
    result = fi_av_open(fabric->domain, &av_attr, &fabric->av, fabric->ctx);
    if (result != FI_SUCCESS)
    {
        fprintf(stderr,
                      "ERROR: could not initialize address vector, failed with %d "
                      "(%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }
    result = fi_ep_bind(fabric->signal, &fabric->av->fid, 0);
    if (result != FI_SUCCESS)
    {
        fprintf(stderr,               
            "ERROR: could not bind endpoint to address vector, failed with "
                      "%d (%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }

    cq_attr.size = 0;
    cq_attr.format = FI_CQ_FORMAT_DATA;
    cq_attr.wait_obj = FI_WAIT_UNSPEC;
    cq_attr.wait_cond = FI_CQ_COND_NONE;
    result =
        fi_cq_open(fabric->domain, &cq_attr, &fabric->cq_signal, fabric->ctx);
    if (result != FI_SUCCESS)
    {
        fprintf(stderr,
            "ERROR: opening completion queue failed with %d (%s). This is fatal.\n",
            result, fi_strerror(result));
        return -1;
    }

    result = fi_ep_bind(fabric->signal, &fabric->cq_signal->fid,
                        FI_TRANSMIT | FI_RECV);
    if (result != FI_SUCCESS)
    {
       fprintf(stderr,               
            "ERROR: could not bind endpoint to completion queue, failed "
                      "with %d (%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }

    result = fi_enable(fabric->signal);
    if (result != FI_SUCCESS)
    {
        fprintf(stderr, 
                      "ERROR: enable endpoint, failed with %d (%s). This is fatal.\n",
                      result, fi_strerror(result));
        return -1;
    }

    fprintf(stderr, "INFO: fabric successfully initialized.\n");

    fi_freeinfo(originfo);

    return 0;
}

static int fini_fabric(struct fabric_state *fabric)
{

    int res;

    fprintf(stderr, "INFO: finalizing fabric...\n");

    do
    {
        res = fi_close((struct fid *)fabric->signal);
    } while (res == -FI_EBUSY);

    if (res != FI_SUCCESS)
    {
        fprintf(stderr,
                      "ERROR: could not close ep, failed with %d (%s).\n", res,
                      fi_strerror(res));
        return -1;
    }

    res = fi_close((struct fid *)fabric->cq_signal);
    if (res != FI_SUCCESS)
    {
        fprintf(stderr, 
                      "ERROR: could not close cq, failed with %d (%s).\n", res,
                      fi_strerror(res));
    }

    res = fi_close((struct fid *)fabric->av);
    if (res != FI_SUCCESS)
    {
        fprintf(stderr,
                      "ERROR: could not close av, failed with %d (%s).\n", res,
                      fi_strerror(res));
    }
    res = fi_close((struct fid *)fabric->domain);
    if (res != FI_SUCCESS)
    {
        fprintf(stderr, 
                      "ERROR: could not close domain, failed with %d (%s).\n", res,
                      fi_strerror(res));
        return -1;
    }

    res = fi_close((struct fid *)fabric->fabric);
    if (res != FI_SUCCESS)
    {
        fprintf(stderr,
                      "ERROR: could not close fabric, failed with %d (%s).\n", res,
                      fi_strerror(res));
        return -1;
    }

    fi_freeinfo(fabric->info);

    if (fabric->ctx)
    {
        free(fabric->ctx);
    }

#ifdef SST_HAVE_CRAY_DRC
    if (Fabric->auth_key)
    {
        free(Fabric->auth_key);
    }
#endif /* SST_HAVE_CRAY_DRC */

    fprintf(stderr, "finalized fabric.\n");

    return 0;
}

int main(int argc, char **argv)
{
    struct fabric_state fabric;
    const char *ifname;

    ifname = NULL;
    if(argc > 1) {
        ifname = argv[1];
    }

    if(init_fabric(&fabric, ifname) == 0) {
        return fini_fabric(&fabric);        
    } else {
        return 1;
    }
}   


