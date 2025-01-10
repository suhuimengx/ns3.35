#include <iostream>
#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"
#include "ns3/scpstp-module.h"
#include "ns3/icmpv4-l4-protocol.h"
#include "ns3/v4ping-helper.h"
#include "ns3/animation-interface.h"
using namespace ns3;


NS_LOG_COMPONENT_DEFINE ("testicmp");

void FailLink(Ptr<Ipv4> ipv4, uint32_t interfaceIndex) {
    Ptr<Ipv4Interface> iface = ipv4->GetObject<Ipv4L3Protocol> ()->GetInterface(interfaceIndex);
    iface->SetDown();
    NS_LOG_UNCOND("Link down at interface: " << interfaceIndex);
}

void UpLink(Ptr<Ipv4> ipv4, uint32_t interfaceIndex) {
    Ptr<Ipv4Interface> iface = ipv4->GetObject<Ipv4L3Protocol> ()->GetInterface(interfaceIndex);
    iface->SetUp();
    NS_LOG_UNCOND("Link up at interface: " << interfaceIndex);
}
void RecomputeRoutingTables() {
    
    Ipv4GlobalRoutingHelper::RecomputeRoutingTables();
    
    NS_LOG_UNCOND("Routing tables recomputed");
}

int main(int argc, char *argv[])
{
  // 设置日志组件
  LogComponentEnable("Icmpv4L4Protocol", LOG_LEVEL_ALL);
  LogComponentEnable("testicmp", LOG_LEVEL_ALL);

  Config::SetDefault("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue(true));

  // 创建节点
  NodeContainer nodes;
  nodes.Create(3);  // 创建三个节点：客户端、路由器、服务器

  // 创建点到点链路设备
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("2ms"));

  // 安装网络设备
  NetDeviceContainer devices1, devices2;
  devices1 = p2p.Install(nodes.Get(0), nodes.Get(1));  // 客户端 -> 路由器
  devices2 = p2p.Install(nodes.Get(1), nodes.Get(2));  // 路由器 -> 服务器



 
  InternetStackHelper internet;

  internet.Install(nodes);

  // 配置IP地址
  Ipv4AddressHelper address1, address2;
  address1.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces1 = address1.Assign(devices1);

  address2.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces2 = address2.Assign(devices2);

  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();//初始化自动计算并填充路由表

  // 客户端安装ping应用
  Time interPacketInterval = Seconds(1);
  V4PingHelper ping(interfaces2.GetAddress(1));
  ping.SetAttribute("Interval", TimeValue(interPacketInterval));
  ApplicationContainer clientApp = ping.Install(nodes.Get(0));
  clientApp.Start(Seconds(1.0));
  clientApp.Stop(Seconds(10.0));

    Simulator::Schedule (Seconds (3.0), &FailLink, interfaces2.Get(1).first, interfaces2.Get(1).second);
  // 更新路由表
    Simulator::Schedule (Seconds (4.0), &RecomputeRoutingTables);

    // 设置回调函数
    //Config::ConnectWithoutContext("/NodeList/0/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&EchoReplyReceived));

  AnimationInterface anim ("ospfd-anim.xml");
  anim.EnablePacketMetadata(true);
  anim.AddSourceDestination(0,"10.1.2.2");
 
  anim.EnableIpv4RouteTracking ("ospfd-tracking.xml", Seconds(0.0), Seconds(10.0), nodes, Seconds(0.1)); 

  // 设置模拟时间
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}
