#include "GeneratedTypes.h"
#ifndef _AMFREQUEST_H_
#define _AMFREQUEST_H_
#include "portal.h"

class AmfRequestProxy : public Portal {
    AmfRequestCb *cb;
public:
    AmfRequestProxy(int id, int tile = DEFAULT_TILE, AmfRequestCb *cbarg = &AmfRequestProxyReq, int bufsize = AmfRequest_reqinfo, PortalPoller *poller = 0) :
        Portal(id, tile, bufsize, NULL, NULL, this, poller), cb(cbarg) {};
    AmfRequestProxy(int id, PortalTransportFunctions *transport, void *param, AmfRequestCb *cbarg = &AmfRequestProxyReq, int bufsize = AmfRequest_reqinfo, PortalPoller *poller = 0) :
        Portal(id, DEFAULT_TILE, bufsize, NULL, NULL, transport, param, this, poller), cb(cbarg) {};
    AmfRequestProxy(int id, PortalPoller *poller) :
        Portal(id, DEFAULT_TILE, AmfRequest_reqinfo, NULL, NULL, NULL, NULL, this, poller), cb(&AmfRequestProxyReq) {};
    int makeReq ( const AmfRequestT req, const uint32_t offset ) { return cb->makeReq (&pint, req, offset); };
    int debugDumpReq ( const uint32_t card ) { return cb->debugDumpReq (&pint, card); };
    int setDmaReadRef ( const uint32_t sgId ) { return cb->setDmaReadRef (&pint, sgId); };
    int setDmaWriteRef ( const uint32_t sgId ) { return cb->setDmaWriteRef (&pint, sgId); };
    int updateMapping ( const uint32_t seg_virtblk, const uint8_t allocated, const uint16_t mapped_block ) { return cb->updateMapping (&pint, seg_virtblk, allocated, mapped_block); };
    int readMapping ( const uint32_t seg_virtblk ) { return cb->readMapping (&pint, seg_virtblk); };
    int updateBlkInfo ( const uint16_t phyaddr_upper, const bsvvector_Luint16_t_L8 blkinfo_vec ) { return cb->updateBlkInfo (&pint, phyaddr_upper, blkinfo_vec); };
    int readBlkInfo ( const uint16_t phyaddr_upper ) { return cb->readBlkInfo (&pint, phyaddr_upper); };
};

extern AmfRequestCb AmfRequest_cbTable;
class AmfRequestWrapper : public Portal {
public:
    AmfRequestWrapper(int id, int tile = DEFAULT_TILE, PORTAL_INDFUNC cba = AmfRequest_handleMessage, int bufsize = AmfRequest_reqinfo, PortalPoller *poller = 0) :
           Portal(id, tile, bufsize, cba, (void *)&AmfRequest_cbTable, this, poller) {
    };
    AmfRequestWrapper(int id, PortalTransportFunctions *transport, void *param, PORTAL_INDFUNC cba = AmfRequest_handleMessage, int bufsize = AmfRequest_reqinfo, PortalPoller *poller=0):
           Portal(id, DEFAULT_TILE, bufsize, cba, (void *)&AmfRequest_cbTable, transport, param, this, poller) {
    };
    AmfRequestWrapper(int id, PortalPoller *poller) :
           Portal(id, DEFAULT_TILE, AmfRequest_reqinfo, AmfRequest_handleMessage, (void *)&AmfRequest_cbTable, this, poller) {
    };
    AmfRequestWrapper(int id, PortalTransportFunctions *transport, void *param, PortalPoller *poller):
           Portal(id, DEFAULT_TILE, AmfRequest_reqinfo, AmfRequest_handleMessage, (void *)&AmfRequest_cbTable, transport, param, this, poller) {
    };
    virtual void disconnect(void) {
        printf("AmfRequestWrapper.disconnect called %d\n", pint.client_fd_number);
    };
    virtual void makeReq ( const AmfRequestT req, const uint32_t offset ) = 0;
    virtual void debugDumpReq ( const uint32_t card ) = 0;
    virtual void setDmaReadRef ( const uint32_t sgId ) = 0;
    virtual void setDmaWriteRef ( const uint32_t sgId ) = 0;
    virtual void updateMapping ( const uint32_t seg_virtblk, const uint8_t allocated, const uint16_t mapped_block ) = 0;
    virtual void readMapping ( const uint32_t seg_virtblk ) = 0;
    virtual void updateBlkInfo ( const uint16_t phyaddr_upper, const bsvvector_Luint16_t_L8 blkinfo_vec ) = 0;
    virtual void readBlkInfo ( const uint16_t phyaddr_upper ) = 0;
};
#endif // _AMFREQUEST_H_
