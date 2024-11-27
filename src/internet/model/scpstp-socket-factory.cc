#include "ns3/scpstp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"


namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (ScpsTpSocketFactory);

TypeId
ScpsTpSocketFactory::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ScpsTpSocketFactory")
    .SetParent<SocketFactory> ()
    .SetGroupName ("Internet")
  ;
  return tid;
}

/*
ScpsTpSocketFactory::ScpsTpSocketFactory ()
{
  NS_LOG_FUNCTION (this);
}

ScpsTpSocketFactory::~ScpsTpSocketFactory ()
{
  NS_LOG_FUNCTION (this);
}

Ptr<Socket>
ScpsTpSocketFactory::CreateSocket (void)
{
  NS_LOG_FUNCTION (this);
  Ptr<ScpsTpSocketBase> socket = CreateObject<ScpsTpSocketBase> ();
  return socket;
}
*/
}  // namespace ns3
