/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef SCPSTP_HELPER_H
#define SCPSTP_HELPER_H


#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/object-factory.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv6-l3-protocol.h"
#include "ns3/internet-trace-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/scpstp-l4-protocol.h"

namespace ns3 {

class ScpsTpHelper : public InternetStackHelper
{
public:
  /**
   * For each node in the input container, aggregate implementations of the
   * ns3::Ipv4, ns3::Ipv6, ns3::Udp, ns3::ScpsTp, and ns3::Tcp classes.  The program will assert
   * if this method is called on a container with a node that already has
   * an Ipv4 object aggregated to it.
   *
   * \param c NodeContainer that holds the set of nodes on which to install the
   * new stacks.
   */
  void InstallScpsTp (NodeContainer c) const;

  /**
   * Aggregate implementations of the ns3::Ipv4, ns3::Ipv6, ns3::Udp, ns3::ScpsTp and ns3::Tcp classes
   * onto the provided node.  This method will assert if called on a node that 
   * already has an Ipv4 object aggregated to it.
   * 
   * \param node The node on which to install the stack.
   */
  void InstallScpsTp (Ptr<Node> node) const;

  /**
   * Aggregate implementations of the ns3::Ipv4, ns3::Ipv6, ns3::Udp, ns3::ScpsTp and ns3::Tcp classes
   * onto the provided node.  This method will assert if called on a node that 
   * already has an Ipv4 object aggregated to it.
   * 
   * \param nodeName The name of the node on which to install the stack.
   */
  void InstallScpsTp (std::string nodeName) const;

private:
  /**
   * \brief create an object from its TypeId and aggregates it to the node
   * \param node the node
   * \param typeId the object TypeId
   */
  static void CreateAndAggregateObjectFromTypeId (Ptr<Node> node, const std::string typeId);
};

}

#endif /* SCPSTP_HELPER_H */

