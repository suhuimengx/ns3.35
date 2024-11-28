#ifndef SCPSTP_L4_PROTOCOL_H
#define SCPSTP_L4_PROTOCOL_H

#include <stdint.h>

#include "ns3/tcp-l4-protocol.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"

namespace ns3 {


class ScpsTpL4Protocol : public TcpL4Protocol
{
public:
 
  static TypeId GetTypeId(void);

 
  ScpsTpL4Protocol();
  virtual ~ScpsTpL4Protocol();


  Ptr<Socket> CreateSocket (void);


  Ptr<Socket> CreateSocket (TypeId congestionTypeId, TypeId recoveryTypeId);


  Ptr<Socket> CreateSocket (TypeId congestionTypeId);

  /**
   * \brief Setup socket factory and callbacks when aggregated to a node
   *
   * This function will notify other components connected to the node that a
   * new stack member is now connected. This will be used to notify Layer 3
   * protocol of layer 4 protocol stack to connect them together.
   * The aggregation is completed by setting the node in the ScpsTp stack, link
   * it to the ipv4 or ipv6 stack and adding ScpsTp socket factory to the node.
   */
  virtual void NotifyNewAggregate ();

protected:
  // 如果需要，添加自己的成员变量

private:
  // 私有成员，暂时不使用
};

} // namespace ns3

#endif // SCPSTP_L4_PROTOCOL_H
