#include "scpstp-l4-protocol.h"
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

#include "tcp-l4-protocol.h"
#include "tcp-header.h"
#include "ipv4-end-point-demux.h"
#include "ipv6-end-point-demux.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv4-l3-protocol.h"
#include "ipv6-l3-protocol.h"
#include "ipv6-routing-protocol.h"
#include "tcp-socket-factory-impl.h"
#include "tcp-socket-base.h"
#include "scpstp-socket-base.h"
#include "tcp-congestion-ops.h"
#include "tcp-cubic.h"
#include "tcp-recovery-ops.h"
#include "tcp-prr-recovery.h"
#include "rtt-estimator.h"

#include <vector>
#include <sstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ScpsTpL4Protocol");


TypeId
ScpsTpL4Protocol::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ScpsTpL4Protocol")
    .SetParent<TcpL4Protocol>()
    .SetGroupName("Internet")
    .AddConstructor<ScpsTpL4Protocol>();
  return tid;
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
  socket->SetTcp (this);
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

} // namespace ns3
