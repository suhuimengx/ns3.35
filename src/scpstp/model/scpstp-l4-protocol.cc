#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/object-vector.h"

#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"

#include "ns3/tcp-l4-protocol.h"
#include "ns3/tcp-header.h"
#include "ns3/ipv4-end-point-demux.h"
#include "ns3/ipv6-end-point-demux.h"
#include "ns3/ipv4-end-point.h"
#include "ns3/ipv6-end-point.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv6-l3-protocol.h"
#include "ns3/ipv6-routing-protocol.h"
#include "ns3/tcp-socket-base.h"
#include "scpstp-socket-base.h"
#include "scpstp-socket-factory-impl.h"
#include "scpstp-l4-protocol.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-cubic.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/tcp-prr-recovery.h"
#include "ns3/rtt-estimator.h"

#include <vector>
#include <sstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ScpsTpL4Protocol");
NS_OBJECT_ENSURE_REGISTERED (ScpsTpL4Protocol);

TypeId
ScpsTpL4Protocol::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ScpsTpL4Protocol")
    .SetParent<TcpL4Protocol>()
    .SetGroupName("Internet")
    .AddConstructor<ScpsTpL4Protocol>();
  return tid;
}

TypeId
ScpsTpL4Protocol::GetInstanceTypeId () const
{
  return ScpsTpL4Protocol::GetTypeId ();
}

ScpsTpL4Protocol::ScpsTpL4Protocol()

{
  NS_LOG_FUNCTION(this);
}

ScpsTpL4Protocol::~ScpsTpL4Protocol()
{
  NS_LOG_FUNCTION(this);
}

Ptr<Socket>
ScpsTpL4Protocol::CreateSocket (TypeId congestionTypeId)
{
  return CreateSocket (congestionTypeId, m_recoveryTypeId);
}

Ptr<Socket>
ScpsTpL4Protocol::CreateSocket (TypeId congestionTypeId, TypeId recoveryTypeId)
{
  NS_LOG_FUNCTION (this << congestionTypeId.GetName ());
  ObjectFactory rttFactory;
  ObjectFactory congestionAlgorithmFactory;
  ObjectFactory recoveryAlgorithmFactory;
  rttFactory.SetTypeId (m_rttTypeId);
  congestionAlgorithmFactory.SetTypeId (congestionTypeId);
  recoveryAlgorithmFactory.SetTypeId (recoveryTypeId);

  Ptr<RttEstimator> rtt = rttFactory.Create<RttEstimator> ();
  Ptr<ScpsTpSocketBase> socket = CreateObject<ScpsTpSocketBase> ();
  Ptr<TcpCongestionOps> algo = congestionAlgorithmFactory.Create<TcpCongestionOps> ();
  Ptr<TcpRecoveryOps> recovery = recoveryAlgorithmFactory.Create<TcpRecoveryOps> ();

  socket->SetNode (m_node);
  socket->SetTcp (this);//这里还没改
  socket->SetRtt (rtt);
  socket->SetCongestionControlAlgorithm (algo);
  socket->SetRecoveryAlgorithm (recovery);

  m_sockets.push_back (socket);
  return socket;
}

Ptr<Socket>
ScpsTpL4Protocol::CreateSocket (void)
{
  return CreateSocket (m_congestionTypeId, m_recoveryTypeId);
}

void
ScpsTpL4Protocol::NotifyNewAggregate ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Node> node = this->GetObject<Node> ();
  Ptr<Ipv4> ipv4 = this->GetObject<Ipv4> ();
  Ptr<Ipv6> ipv6 = node->GetObject<Ipv6> ();

  if (m_node == 0)
    {
      if ((node != 0) && (ipv4 != 0 || ipv6 != 0))
        {
          this->SetNode (node);
          Ptr<ScpsTpSocketFactoryImpl> scpstpFactory = CreateObject<ScpsTpSocketFactoryImpl> ();
          scpstpFactory->SetScpsTp (this);
          node->AggregateObject (scpstpFactory);
        }
    }

  // We set at least one of our 2 down targets to the IPv4/IPv6 send
  // functions.  Since these functions have different prototypes, we
  // need to keep track of whether we are connected to an IPv4 or
  // IPv6 lower layer and call the appropriate one.

  if (ipv4 != 0 && m_downTarget.IsNull ())
    {
      ipv4->Insert (this);
      this->SetDownTarget (MakeCallback (&Ipv4::Send, ipv4));
    }
  if (ipv6 != 0 && m_downTarget6.IsNull ())
    {
      ipv6->Insert (this);
      this->SetDownTarget6 (MakeCallback (&Ipv6::Send, ipv6));
    }
  IpL4Protocol::NotifyNewAggregate ();
}

} // namespace ns3
