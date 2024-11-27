#ifndef SCPSTP_SOCKET_FACTORY_IMPL_H
#define SCPSTP_SOCKET_FACTORY_IMPL_H

#include "ns3/scpstp-socket-factory.h"
#include "ns3/ptr.h"
#include "scpstp-l4-protocol.h"

namespace ns3 {



class ScpsTpSocketFactoryImpl : public ScpsTpSocketFactory
{
public:
    ScpsTpSocketFactoryImpl ();
    virtual ~ScpsTpSocketFactoryImpl ();

    void SetScpsTp (Ptr<ScpsTpL4Protocol> scpstp);

    virtual Ptr<Socket> CreateSocket (void);
protected:
    virtual void DoDispose (void);
private:
    Ptr<ScpsTpL4Protocol> m_scpstp; //!< the associated ScpsTp L4 protocol
};

}
#endif
