/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << " [node " << m_node->GetId () << "] "; }

#include "scpstp-socket-base.h"
#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv6.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv6-routing-protocol.h"
#include "ns3/simulation-singleton.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/data-rate.h"
#include "ns3/object.h"
#include "tcp-l4-protocol.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv6-l3-protocol.h"
#include "tcp-tx-buffer.h"
#include "tcp-rx-buffer.h"
#include "rtt-estimator.h"
#include "tcp-header.h"
#include "tcp-option-winscale.h"
#include "tcp-option-ts.h"
#include "tcp-option-sack-permitted.h"
#include "tcp-option-sack.h"
#include "tcp-congestion-ops.h"
#include "tcp-recovery-ops.h"
#include "ns3/tcp-rate-ops.h"

#include <math.h>
#include <algorithm>

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("ScpsTpSocketBase");

NS_OBJECT_ENSURE_REGISTERED (ScpsTpSocketBase);

TypeId 
ScpsTpSocketBase::GetTypeId(void)
{
  static TypeId tid = TypeId("ns3::ScpsTpSocketBase")
    .SetParent<TcpSocketBase>()
    .SetGroupName("Internet")
    .AddConstructor<ScpsTpSocketBase>()
    .AddAttribute("LossType",
                  "Reason for data loss",
                  EnumValue(Corruption),
                  MakeEnumAccessor(&ScpsTpSocketBase::m_lossType),
                  MakeEnumChecker(LossType::Corruption, "Corruption",
                                  LossType::Congestion, "Congestion",
                                  LossType::Link_Outage, "Link_Outage"));
    /*
    .AddTraceSource("LossType",
                    "Reason for data loss",
                    MakeTraceSourceAccessor (&ScpsTpSocketBase::m_lossType),
                    "ns3::EnumValueCallback::String"
                    );*/
  return tid;
}

TypeId
ScpsTpSocketBase::GetInstanceTypeId () const
{
  return ScpsTpSocketBase::GetTypeId ();
}

ScpsTpSocketBase::ScpsTpSocketBase(void)
  : TcpSocketBase ()
{
  NS_LOG_FUNCTION (this);
  m_lossType = Corruption;

}

ScpsTpSocketBase::ScpsTpSocketBase(const ScpsTpSocketBase &sock)
  : TcpSocketBase (sock),
    m_lossType (sock.m_lossType)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
}

ScpsTpSocketBase::~ScpsTpSocketBase()
{
  NS_LOG_FUNCTION (this);
}


ScpsTpSocketBase::LossType
ScpsTpSocketBase::GetLossType(void) const
{
  return m_lossType;
}

void
ScpsTpSocketBase::SetLossType(ScpsTpSocketBase::LossType losstype)
{
  NS_LOG_FUNCTION (this << losstype);
  m_lossType = losstype;
}


}