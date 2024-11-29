/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "scpstp-helper.h"
#include "ns3/names.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ScpsTpHelper");

void 
ScpsTpHelper::InstallScpsTp (NodeContainer c) const
{
 
  for (NodeContainer::Iterator i = c.Begin (); i !=c.End (); ++i)
  {
    InstallScpsTp (*i);
  }
}

void
ScpsTpHelper::InstallScpsTp (Ptr<Node> node) const
{
   NS_LOG_INFO("stack install");
  Install (node);//目前两种解决方法，把这里面的Tcp协议删掉，或者放弃继承TcpL4Protocol
  NS_LOG_INFO("ok " << node);
  CreateAndAggregateObjectFromTypeId (node, "ns3::ScpsTpL4Protocol");
}

void
ScpsTpHelper::InstallScpsTp (std::string nodeName) const
{
  Ptr<Node> node = Names::Find<Node> (nodeName);
  InstallScpsTp (node);
}

void
ScpsTpHelper::CreateAndAggregateObjectFromTypeId (Ptr<Node> node, const std::string typeId)
{
  ObjectFactory factory;
  factory.SetTypeId (typeId);
  Ptr<Object> protocol = factory.Create <Object> ();
  node->AggregateObject (protocol);
}
}

