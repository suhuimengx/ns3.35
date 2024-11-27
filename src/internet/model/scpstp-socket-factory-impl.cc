#include "scpstp-socket-factory-impl.h"
#include "scpstp-l4-protocol.h"
#include "ns3/socket.h"
#include "ns3/assert.h"

namespace ns3 {

ScpsTpSocketFactoryImpl::ScpsTpSocketFactoryImpl ()  
    : m_scpstp (0)
{
}

ScpsTpSocketFactoryImpl::~ScpsTpSocketFactoryImpl ()
{
  NS_ASSERT (m_scpstp == 0);  
}

Ptr<Socket>
ScpsTpSocketFactoryImpl::CreateSocket (void)
{
  return m_scpstp->CreateSocket ();
}

void
ScpsTpSocketFactoryImpl::SetScpsTp (Ptr<ScpsTpL4Protocol> scpstp)
{
  m_scpstp = scpstp;
}
void 
ScpsTpSocketFactoryImpl::DoDispose (void)
{
  m_scpstp = 0;
  ScpsTpSocketFactory::DoDispose ();
}

} // namespace ns3