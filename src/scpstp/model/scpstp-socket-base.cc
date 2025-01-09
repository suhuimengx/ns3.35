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
#include "ns3/tcp-l4-protocol.h"
#include "ns3/ipv4-end-point.h"
#include "ns3/ipv6-end-point.h"
#include "ns3/ipv6-l3-protocol.h"
#include "ns3/tcp-tx-buffer.h"
#include "ns3/tcp-rx-buffer.h"
#include "ns3/rtt-estimator.h"
#include "ns3/tcp-header.h"
#include "ns3/tcp-option-winscale.h"
#include "ns3/tcp-option-ts.h"
#include "ns3/tcp-option-sack-permitted.h"
#include "ns3/tcp-option-sack.h"
#include "ns3/tcp-congestion-ops.h"
#include "ns3/tcp-recovery-ops.h"
#include "ns3/tcp-rate-ops.h"
#include "scpstp-socket-base.h"
#include "scpstp-rx-buffer.h"
#include "scpstp-tx-buffer.h"
#include "scpstp-socket-state.h"
#include "scpstp-option-snack.h"

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
                                  LossType::Link_Outage, "Link_Outage"))
    .AddTraceSource("LossType",
                    "Reason for data loss",
                    MakeTraceSourceAccessor (&ScpsTpSocketBase::m_lossType),
                    "ns3::EnumValueCallback::String"
                    );
  return tid;
}

TypeId
ScpsTpSocketBase::GetInstanceTypeId () const
{
  return ScpsTpSocketBase::GetTypeId ();
}

ScpsTpSocketBase::ScpsTpSocketBase(void)
  : TcpSocketBase (),
    m_lossType (Corruption),
    m_isCorruptionRecovery (false)
{
  NS_LOG_FUNCTION (this);
  m_txBuffer = CreateObject<ScpsTpTxBuffer> ();
  m_txBuffer->HeadSequence ();
  m_txBuffer->SetRWndCallback (MakeCallback (&ScpsTpSocketBase::GetRWnd, this));
  m_tcb      = CreateObject<TcpSocketState> ();
  m_rateOps  = CreateObject <TcpRateLinux> ();

  m_tcb->m_rxBuffer = CreateObject<ScpsTpRxBuffer> ();
  //检查m_rxBuffer是否是ScpsTpRxBuffer的实例
  NS_ASSERT (m_tcb->m_rxBuffer->GetInstanceTypeId () == ScpsTpRxBuffer::GetTypeId ());
  m_tcb->m_pacingRate = m_tcb->m_maxPacingRate;
  m_pacingTimer.SetFunction (&ScpsTpSocketBase::NotifyPacingPerformed, this);

  m_tcb->m_sendEmptyPacketCallback = MakeCallback (&ScpsTpSocketBase::SendEmptyPacket, this);

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext ("PacingRate",
                                          MakeCallback (&ScpsTpSocketBase::UpdatePacingRateTrace, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindow",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCwnd, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindowInflated",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCwndInfl, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("SlowStartThreshold",
                                          MakeCallback (&ScpsTpSocketBase::UpdateSsThresh, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("CongState",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCongState, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("EcnState",
                                          MakeCallback (&ScpsTpSocketBase::UpdateEcnState, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("NextTxSequence",
                                          MakeCallback (&ScpsTpSocketBase::UpdateNextTxSequence, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("HighestSequence",
                                          MakeCallback (&ScpsTpSocketBase::UpdateHighTxMark, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("BytesInFlight",
                                          MakeCallback (&ScpsTpSocketBase::UpdateBytesInFlight, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("RTT",
                                          MakeCallback (&ScpsTpSocketBase::UpdateRtt, this));
  NS_ASSERT (ok == true);
}

ScpsTpSocketBase::ScpsTpSocketBase(const ScpsTpSocketBase &sock)
  : TcpSocketBase (sock),
    m_lossType (sock.m_lossType),
    m_scpstp (sock.m_scpstp),
    m_isCorruptionRecovery (sock.m_isCorruptionRecovery)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
    // Copy the rtt estimator if it is set
  if (sock.m_rtt)
    {
      m_rtt = sock.m_rtt->Copy ();
    }
  // Reset all callbacks to null
  Callback<void, Ptr< Socket > > vPS = MakeNullCallback<void, Ptr<Socket> > ();
  Callback<void, Ptr<Socket>, const Address &> vPSA = MakeNullCallback<void, Ptr<Socket>, const Address &> ();
  Callback<void, Ptr<Socket>, uint32_t> vPSUI = MakeNullCallback<void, Ptr<Socket>, uint32_t> ();
  SetConnectCallback (vPS, vPS);
  SetDataSentCallback (vPSUI);
  SetSendCallback (vPSUI);
  SetRecvCallback (vPS);
  //m_txBuffer = CopyObject (dynamic_cast<Ptr<ScpsTpTxBuffer> >(sock.m_txBuffer));

  m_txBuffer = Ptr<TcpTxBuffer> (new ScpsTpTxBuffer (*PeekPointer (sock.m_txBuffer)), false);
  m_txBuffer->SetRWndCallback (MakeCallback (&ScpsTpSocketBase::GetRWnd, this));
  m_tcb = CopyObject (sock.m_tcb);
  //m_tcb->m_rxBuffer = CopyObject (sock.m_tcb->m_rxBuffer);
  m_tcb->m_rxBuffer = Ptr<TcpRxBuffer> (new ScpsTpRxBuffer (*PeekPointer (sock.m_tcb->m_rxBuffer)), false);
  NS_ASSERT (m_tcb->m_rxBuffer->GetInstanceTypeId () == ScpsTpRxBuffer::GetTypeId ());
  m_tcb->m_pacingRate = m_tcb->m_maxPacingRate;
  m_pacingTimer.SetFunction (&ScpsTpSocketBase::NotifyPacingPerformed, this);

  if (sock.m_congestionControl)
    {
      m_congestionControl = sock.m_congestionControl->Fork ();
      m_congestionControl->Init (m_tcb);
    }

  if (sock.m_recoveryOps)
    {
      m_recoveryOps = sock.m_recoveryOps->Fork ();
    }

  m_rateOps = CreateObject <TcpRateLinux> ();
  if (m_tcb->m_sendEmptyPacketCallback.IsNull ())
    {
      m_tcb->m_sendEmptyPacketCallback = MakeCallback (&ScpsTpSocketBase::SendEmptyPacket, this);
    }

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext ("PacingRate",
                                          MakeCallback (&ScpsTpSocketBase::UpdatePacingRateTrace, this));

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindow",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCwnd, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindowInflated",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCwndInfl, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("SlowStartThreshold",
                                          MakeCallback (&ScpsTpSocketBase::UpdateSsThresh, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("CongState",
                                          MakeCallback (&ScpsTpSocketBase::UpdateCongState, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("EcnState",
                                          MakeCallback (&ScpsTpSocketBase::UpdateEcnState, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("NextTxSequence",
                                          MakeCallback (&ScpsTpSocketBase::UpdateNextTxSequence, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("HighestSequence",
                                          MakeCallback (&ScpsTpSocketBase::UpdateHighTxMark, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("BytesInFlight",
                                          MakeCallback (&ScpsTpSocketBase::UpdateBytesInFlight, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("RTT",
                                          MakeCallback (&ScpsTpSocketBase::UpdateRtt, this));
  NS_ASSERT (ok == true);
}


ScpsTpSocketBase::~ScpsTpSocketBase()
{
  NS_LOG_FUNCTION (this);
  if (m_endPoint != nullptr)
    {
      NS_ASSERT (m_scpstp != nullptr);
      /*
       * Upon Bind, an Ipv4Endpoint is allocated and set to m_endPoint, and
       * DestroyCallback is set to ScpsTpSocketBase::Destroy. If we called
       * m_scpstp->DeAllocate, it will destroy its Ipv4EndpointDemux::DeAllocate,
       * which in turn destroys my m_endPoint, and in turn invokes
       * ScpsTpSocketBase::Destroy to nullify m_node, m_endPoint, and m_scpstp.
       */
      NS_ASSERT (m_endPoint != nullptr);
      m_scpstp->DeAllocate (m_endPoint);
      NS_ASSERT (m_endPoint == nullptr);
    }
  if (m_endPoint6 != nullptr)
    {
      NS_ASSERT (m_scpstp != nullptr);
      NS_ASSERT (m_endPoint6 != nullptr);
      m_scpstp->DeAllocate (m_endPoint6);
      NS_ASSERT (m_endPoint6 == nullptr);
    }
  m_scpstp = 0;
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
  if(m_lossType == LossType::Link_Outage && m_linkOutPersistEvent.IsExpired ())
    {
    NS_LOG_LOGIC (this << " Enter linkout persist state");
    NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                  (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
    m_retxEvent.Cancel ();
    NS_LOG_LOGIC ("Schedule persist timeout at time " <<
                  Simulator::Now ().GetSeconds () << " to expire at time " <<
                  (Simulator::Now () + m_persistTimeout).GetSeconds ());
    m_linkOutPersistEvent = Simulator::Schedule (m_persistTimeout, &ScpsTpSocketBase::LinkOutPersistTimeout, this);
    NS_ASSERT (m_persistTimeout == Simulator::GetDelayLeft (m_linkOutPersistEvent));

    }
}

/* Associate the L4 protocol (e.g. mux/demux) with this socket */
void
ScpsTpSocketBase::SetScpsTp (Ptr<ScpsTpL4Protocol> scpstp)
{
  m_scpstp = scpstp;
}

/* Inherit from Socket class: Bind socket to an end-point in ScpsTpL4Protocol */
int
ScpsTpSocketBase::Bind (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = m_scpstp->Allocate ();
  if (0 == m_endPoint)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }

  m_scpstp->AddSocket (this);

  return SetupCallback ();
}

int
ScpsTpSocketBase::Bind6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = m_scpstp->Allocate6 ();
  if (0 == m_endPoint6)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }

  m_scpstp->AddSocket (this);

  return SetupCallback ();
}

/* Inherit from Socket class: Bind socket (with specific address) to an end-point in ScpsTpL4Protocol */
int
ScpsTpSocketBase::Bind (const Address &address)
{
  NS_LOG_FUNCTION (this << address);
  if (InetSocketAddress::IsMatchingType (address))
    {
      InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
      Ipv4Address ipv4 = transport.GetIpv4 ();
      uint16_t port = transport.GetPort ();
      SetIpTos (transport.GetTos ());
      if (ipv4 == Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_scpstp->Allocate ();
        }
      else if (ipv4 == Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_scpstp->Allocate (GetBoundNetDevice (), port);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_scpstp->Allocate (ipv4);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_scpstp->Allocate (GetBoundNetDevice (), ipv4, port);
        }
      if (0 == m_endPoint)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else if (Inet6SocketAddress::IsMatchingType (address))
    {
      Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom (address);
      Ipv6Address ipv6 = transport.GetIpv6 ();
      uint16_t port = transport.GetPort ();
      if (ipv6 == Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_scpstp->Allocate6 ();
        }
      else if (ipv6 == Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_scpstp->Allocate6 (GetBoundNetDevice (), port);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_scpstp->Allocate6 (ipv6);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_scpstp->Allocate6 (GetBoundNetDevice (), ipv6, port);
        }
      if (0 == m_endPoint6)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  m_scpstp->AddSocket (this);

  NS_LOG_LOGIC ("ScpsTpSocketBase " << this << " got an endpoint: " << m_endPoint);

  return SetupCallback ();
}

void
ScpsTpSocketBase::DoForwardUp (Ptr<Packet> packet, const Address &fromAddress,
                            const Address &toAddress)
{
  // in case the packet still has a priority tag attached, remove it
  SocketPriorityTag priorityTag;
  packet->RemovePacketTag (priorityTag);

  // Peel off TCP header
  TcpHeader tcpHeader;
  packet->RemoveHeader (tcpHeader);
  SequenceNumber32 seq = tcpHeader.GetSequenceNumber ();

  if (m_state == ESTABLISHED && !(tcpHeader.GetFlags () & TcpHeader::RST))
    {
      // Check if the sender has responded to ECN echo by reducing the Congestion Window
      if (tcpHeader.GetFlags () & TcpHeader::CWR )
        {
          // Check if a packet with CE bit set is received. If there is no CE bit set, then change the state to ECN_IDLE to
          // stop sending ECN Echo messages. If there is CE bit set, the packet should continue sending ECN Echo messages
          //
          if (m_tcb->m_ecnState != TcpSocketState::ECN_CE_RCVD)
            {
              NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_IDLE");
              m_tcb->m_ecnState = TcpSocketState::ECN_IDLE;
            }
        }
    }

  m_rxTrace (packet, tcpHeader, this);

  if (tcpHeader.GetFlags () & TcpHeader::SYN)
    {
      /* The window field in a segment where the SYN bit is set (i.e., a <SYN>
       * or <SYN,ACK>) MUST NOT be scaled (from RFC 7323 page 9). But should be
       * saved anyway..
       */
      m_rWnd = tcpHeader.GetWindowSize ();

      if (tcpHeader.HasOption (TcpOption::WINSCALE) && m_winScalingEnabled)
        {
          ProcessOptionWScale (tcpHeader.GetOption (TcpOption::WINSCALE));
        }
      else
        {
          m_winScalingEnabled = false;
        }

      if (tcpHeader.HasOption (TcpOption::SACKPERMITTED) && m_sackEnabled)
        {
          ProcessOptionSackPermitted (tcpHeader.GetOption (TcpOption::SACKPERMITTED));
        }
      else
        {
          m_sackEnabled = false;
          m_txBuffer->SetSackEnabled (false);
        }

      // When receiving a <SYN> or <SYN-ACK> we should adapt TS to the other end
      if (tcpHeader.HasOption (TcpOption::TS) && m_timestampEnabled)
        {
          ProcessOptionTimestamp (tcpHeader.GetOption (TcpOption::TS),
                                  tcpHeader.GetSequenceNumber ());
        }
      else
        {
          m_timestampEnabled = false;
        }

      // Initialize cWnd and ssThresh
      m_tcb->m_cWnd = GetInitialCwnd () * GetSegSize ();
      m_tcb->m_cWndInfl = m_tcb->m_cWnd;
      m_tcb->m_ssThresh = GetInitialSSThresh ();

      if (tcpHeader.GetFlags () & TcpHeader::ACK)
        {
          EstimateRtt (tcpHeader);
          m_highRxAckMark = tcpHeader.GetAckNumber ();
        }
    }
  else if (tcpHeader.GetFlags () & TcpHeader::ACK)
    {
      NS_ASSERT (!(tcpHeader.GetFlags () & TcpHeader::SYN));
      if (m_timestampEnabled)
        {
          if (!tcpHeader.HasOption (TcpOption::TS))
            {
              // Ignoring segment without TS, RFC 7323
              NS_LOG_LOGIC ("At state " << TcpStateName[m_state] <<
                            " received packet of seq [" << seq <<
                            ":" << seq + packet->GetSize () <<
                            ") without TS option. Silently discard it");
              return;
            }
          else
            {
              ProcessOptionTimestamp (tcpHeader.GetOption (TcpOption::TS),
                                      tcpHeader.GetSequenceNumber ());
            }
        }

      EstimateRtt (tcpHeader);
      UpdateWindowSize (tcpHeader);
    }


  if (m_rWnd.Get () == 0 && m_persistEvent.IsExpired ())
    { // Zero window: Enter persist state to send 1 byte to probe
      NS_LOG_LOGIC (this << " Enter zerowindow persist state");
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      NS_LOG_LOGIC ("Schedule persist timeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_persistTimeout).GetSeconds ());
      m_persistEvent = Simulator::Schedule (m_persistTimeout, &ScpsTpSocketBase::PersistTimeout, this);
      NS_ASSERT (m_persistTimeout == Simulator::GetDelayLeft (m_persistEvent));
    }

  // TCP state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  switch (m_state)
    {
    case ESTABLISHED:
      ProcessEstablished (packet, tcpHeader);
      break;
    case LISTEN:
      ProcessListen (packet, tcpHeader, fromAddress, toAddress);
      break;
    case TIME_WAIT:
      // Do nothing
      break;
    case CLOSED:
      // Send RST if the incoming packet is not a RST
      if ((tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG)) != TcpHeader::RST)
        { // Since m_endPoint is not configured yet, we cannot use SendRST here
          TcpHeader h;
          Ptr<Packet> p = Create<Packet> ();
          h.SetFlags (TcpHeader::RST);
          h.SetSequenceNumber (m_tcb->m_nextTxSequence);
          h.SetAckNumber (m_tcb->m_rxBuffer->NextRxSequence ());
          h.SetSourcePort (tcpHeader.GetDestinationPort ());
          h.SetDestinationPort (tcpHeader.GetSourcePort ());
          h.SetWindowSize (AdvertisedWindowSize ());
          AddOptions (h);
          m_txTrace (p, h, this);
          m_scpstp->SendPacket (p, h, toAddress, fromAddress, m_boundnetdevice);
        }
      break;
    case SYN_SENT:
      ProcessSynSent (packet, tcpHeader);
      break;
    case SYN_RCVD:
      ProcessSynRcvd (packet, tcpHeader, fromAddress, toAddress);
      break;
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case CLOSE_WAIT:
      ProcessWait (packet, tcpHeader);
      break;
    case CLOSING:
      ProcessClosing (packet, tcpHeader);
      break;
    case LAST_ACK:
      ProcessLastAck (packet, tcpHeader);
      break;
    default: // mute compiler
      break;
    }

  if (m_rWnd.Get () != 0 && m_persistEvent.IsRunning ())
    { // persist probes end, the other end has increased the window
      NS_ASSERT (m_connected);
      NS_LOG_LOGIC (this << " Leaving zerowindow persist state");
      m_persistEvent.Cancel ();

      SendPendingData (m_connected);
    }
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
ScpsTpSocketBase::Destroy (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = nullptr;
  if (m_scpstp != nullptr)
    {
      m_scpstp->RemoveSocket (this);
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
ScpsTpSocketBase::Destroy6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = nullptr;
  if (m_scpstp != nullptr)
    {
      m_scpstp->RemoveSocket (this);
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Send an empty packet with specified TCP flags */
void
ScpsTpSocketBase::SendEmptyPacket (uint8_t flags)
{
  NS_LOG_FUNCTION (this << static_cast<uint32_t> (flags));

  if (m_endPoint == nullptr && m_endPoint6 == nullptr)
    {
      NS_LOG_WARN ("Failed to send empty packet due to null endpoint");
      return;
    }

  Ptr<Packet> p = Create<Packet> ();
  TcpHeader header;
  SequenceNumber32 s = m_tcb->m_nextTxSequence;

  if (flags & TcpHeader::FIN)
    {
      flags |= TcpHeader::ACK;
    }
  else if (m_state == FIN_WAIT_1 || m_state == LAST_ACK || m_state == CLOSING)
    {
      ++s;
    }

  AddSocketTags (p);

  header.SetFlags (flags);
  header.SetSequenceNumber (s);
  header.SetAckNumber (m_tcb->m_rxBuffer->NextRxSequence ());
  if (m_endPoint != nullptr)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (header);

  // RFC 6298, clause 2.4
  m_rto = Max (m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4), m_minRto);

  uint16_t windowSize = AdvertisedWindowSize ();
  bool hasSyn = flags & TcpHeader::SYN;
  bool hasFin = flags & TcpHeader::FIN;
  bool isAck = flags == TcpHeader::ACK;
  if (hasSyn)
    {
      if (m_winScalingEnabled)
        { // The window scaling option is set only on SYN packets
          AddOptionWScale (header);
        }

      if (m_sackEnabled)
        {
          AddOptionSackPermitted (header);
        }

      if (m_synCount == 0)
        { // No more connection retries, give up
          NS_LOG_LOGIC ("Connection failed.");
          m_rtt->Reset (); //According to recommendation -> RFC 6298
          NotifyConnectionFailed ();
          m_state = CLOSED;
          DeallocateEndPoint ();
          return;
        }
      else
        { // Exponential backoff of connection time out
          int backoffCount = 0x1 << (m_synRetries - m_synCount);
          m_rto = m_cnTimeout * backoffCount;
          m_synCount--;
        }

      if (m_synRetries - 1 == m_synCount)
        {
          UpdateRttHistory (s, 0, false);
        }
      else
        { // This is SYN retransmission
          UpdateRttHistory (s, 0, true);
        }

      windowSize = AdvertisedWindowSize (false);
    }
  header.SetWindowSize (windowSize);

  if (flags & TcpHeader::ACK)
    { // If sending an ACK, cancel the delay ACK as well
      m_delAckEvent.Cancel ();
      m_delAckCount = 0;
      if (m_highTxAck < header.GetAckNumber ())
        {
          m_highTxAck = header.GetAckNumber ();
        }
      if (m_sackEnabled && m_tcb->m_rxBuffer->GetSackListSize () > 0)
        {
          AddOptionSack (header);
        }
      NS_LOG_INFO ("Sending a pure ACK, acking seq " << m_tcb->m_rxBuffer->NextRxSequence ());
    }

  m_txTrace (p, header, this);

  if (m_endPoint != nullptr)
    {
      m_scpstp->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_scpstp->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }


  if (m_retxEvent.IsExpired () && (hasSyn || hasFin) && !isAck )
    { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
      NS_LOG_LOGIC ("Schedule retransmission timeout at time "
                    << Simulator::Now ().GetSeconds () << " to expire at time "
                    << (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &ScpsTpSocketBase::SendEmptyPacket, this, flags);
    }
}

/* Deallocate the end point and cancel all the timers */
void
ScpsTpSocketBase::DeallocateEndPoint (void)
{
  if (m_endPoint != nullptr)
    {
      CancelAllTimers ();
      m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
      m_scpstp->DeAllocate (m_endPoint);
      m_endPoint = nullptr;
      m_scpstp->RemoveSocket (this);
    }
  else if (m_endPoint6 != nullptr)
    {
      CancelAllTimers ();
      m_endPoint6->SetDestroyCallback (MakeNullCallback<void> ());
      m_scpstp->DeAllocate (m_endPoint6);
      m_endPoint6 = nullptr;
      m_scpstp->RemoveSocket (this);
    }
}

/* This function is called only if a SYN received in LISTEN state. After
   ScpsTpSocketBase cloned, allocate a new end point to handle the incoming
   connection and send a SYN+ACK to complete the handshake. */
void
ScpsTpSocketBase::CompleteFork (Ptr<Packet> p, const TcpHeader& h,
                             const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << p << h << fromAddress << toAddress);
  NS_UNUSED (p);
  // Get port and address from peer (connecting host)
  if (InetSocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint = m_scpstp->Allocate (GetBoundNetDevice (),
                                    InetSocketAddress::ConvertFrom (toAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (toAddress).GetPort (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint6 = nullptr;
    }
  else if (Inet6SocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint6 = m_scpstp->Allocate6 (GetBoundNetDevice (),
                                      Inet6SocketAddress::ConvertFrom (toAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (toAddress).GetPort (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint = nullptr;
    }
  m_scpstp->AddSocket (this);

  // Change the cloned socket from LISTEN state to SYN_RCVD
  NS_LOG_DEBUG ("LISTEN -> SYN_RCVD");
  m_state = SYN_RCVD;
  m_synCount = m_synRetries;
  m_dataRetrCount = m_dataRetries;
  SetupCallback ();
  // Set the sequence number and send SYN+ACK
  m_tcb->m_rxBuffer->SetNextRxSequence (h.GetSequenceNumber () + SequenceNumber32 (1));

  /* Check if we received an ECN SYN packet. Change the ECN state of receiver to ECN_IDLE if sender has sent an ECN SYN
   * packet and the traffic is ECN Capable
   */
  if (m_tcb->m_useEcn != TcpSocketState::Off &&
      (h.GetFlags () & (TcpHeader::CWR | TcpHeader::ECE)) == (TcpHeader::CWR | TcpHeader::ECE))
    {
      SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK | TcpHeader::ECE);
      NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_IDLE");
      m_tcb->m_ecnState = TcpSocketState::ECN_IDLE;
    }
  else
    {
      SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);
      m_tcb->m_ecnState = TcpSocketState::ECN_DISABLED;
    }
}

/* Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
    TCP header, and send to ScpsTpL4Protocol */
uint32_t
ScpsTpSocketBase::SendDataPacket (SequenceNumber32 seq, uint32_t maxSize, bool withAck)
{
  NS_LOG_FUNCTION (this << seq << maxSize << withAck);

  bool isStartOfTransmission = BytesInFlight () == 0U;
  TcpTxItem *outItem = m_txBuffer->CopyFromSequence (maxSize, seq);

  m_rateOps->SkbSent(outItem, isStartOfTransmission);

  bool isRetransmission = outItem->IsRetrans ();
  Ptr<Packet> p = outItem->GetPacketCopy ();
  uint32_t sz = p->GetSize (); // Size of packet
  uint8_t flags = withAck ? TcpHeader::ACK : 0;
  uint32_t remainingData = m_txBuffer->SizeFromSequence (seq + SequenceNumber32 (sz));

  // ScpsTp sender should not send data out of the window advertised by the
  // peer when it is not retransmission.
  NS_ASSERT (isRetransmission || ((m_highRxAckMark + SequenceNumber32 (m_rWnd)) >= (seq + SequenceNumber32 (maxSize))));

  if (IsPacingEnabled ())
    {
      NS_LOG_INFO ("Pacing is enabled");
      if (m_pacingTimer.IsExpired ())
        {
          NS_LOG_DEBUG ("Current Pacing Rate " << m_tcb->m_pacingRate);
          NS_LOG_DEBUG ("Timer is in expired state, activate it " << m_tcb->m_pacingRate.Get ().CalculateBytesTxTime (sz));
          m_pacingTimer.Schedule (m_tcb->m_pacingRate.Get ().CalculateBytesTxTime (sz));
        }
      else
        {
          NS_LOG_INFO ("Timer is already in running state");
        }
    }
  else
    {
      NS_LOG_INFO ("Pacing is disabled");
    }

  if (withAck)
    {
      m_delAckEvent.Cancel ();
      m_delAckCount = 0;
    }

  if (m_tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD && m_ecnEchoSeq.Get() > m_ecnCWRSeq.Get () && !isRetransmission)
    {
      NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_CWR_SENT");
      m_tcb->m_ecnState = TcpSocketState::ECN_CWR_SENT;
      m_ecnCWRSeq = seq;
      flags |= TcpHeader::CWR;
      NS_LOG_INFO ("CWR flags set");
    }

  AddSocketTags (p);

  if (m_closeOnEmpty && (remainingData == 0))
    {
      flags |= TcpHeader::FIN;
      if (m_state == ESTABLISHED)
        { // On active close: I am the first one to send FIN
          NS_LOG_DEBUG ("ESTABLISHED -> FIN_WAIT_1");
          m_state = FIN_WAIT_1;
        }
      else if (m_state == CLOSE_WAIT)
        { // On passive close: Peer sent me FIN already
          NS_LOG_DEBUG ("CLOSE_WAIT -> LAST_ACK");
          m_state = LAST_ACK;
        }
    }
  TcpHeader header;
  header.SetFlags (flags);
  header.SetSequenceNumber (seq);
  header.SetAckNumber (m_tcb->m_rxBuffer->NextRxSequence ());
  if (m_endPoint)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  header.SetWindowSize (AdvertisedWindowSize ());
  AddOptions (header);

  if (m_retxEvent.IsExpired ())
    {
      // Schedules retransmit timeout. m_rto should be already doubled.

      NS_LOG_LOGIC (this << " SendDataPacket Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds () );
      m_retxEvent = Simulator::Schedule (m_rto, &ScpsTpSocketBase::ReTxTimeout, this);
    }

  m_txTrace (p, header, this);

  if (m_endPoint)
    {
      m_scpstp->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
      NS_LOG_DEBUG ("Send segment of size " << sz << " with remaining data " <<
                    remainingData << " via ScpsTpL4Protocol to " <<  m_endPoint->GetPeerAddress () <<
                    ". Header " << header);
    }
  else
    {
      m_scpstp->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
      NS_LOG_DEBUG ("Send segment of size " << sz << " with remaining data " <<
                    remainingData << " via ScpsTpL4Protocol to " <<  m_endPoint6->GetPeerAddress () <<
                    ". Header " << header);
    }

  UpdateRttHistory (seq, sz, isRetransmission);

  // Update bytes sent during recovery phase
  if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY || m_tcb->m_congState == TcpSocketState::CA_CWR)
    {
      m_recoveryOps->UpdateBytesSent (sz);
    }

  // Notify the application of the data being sent unless this is a retransmit
  if (!isRetransmission)
    {
      Simulator::ScheduleNow (&ScpsTpSocketBase::NotifyDataSent, this,
                              (seq + sz - m_tcb->m_highTxMark.Get ()));
    }
  // Update highTxMark
  m_tcb->m_highTxMark = std::max (seq + sz, m_tcb->m_highTxMark.Get ());
  return sz;
}

// Send 1-byte data to probe for the window size at the receiver when
// the local knowledge tells that the receiver has zero window size
// C.f.: RFC793 p.42, RFC1112 sec.4.2.2.17
void
ScpsTpSocketBase::PersistTimeout ()
{
  NS_LOG_LOGIC ("PersistTimeout expired at " << Simulator::Now ().GetSeconds ());
  m_persistTimeout = std::min (Seconds (60), Time (2 * m_persistTimeout)); // max persist timeout = 60s
  Ptr<Packet> p = m_txBuffer->CopyFromSequence (1, m_tcb->m_nextTxSequence)->GetPacketCopy ();
  m_txBuffer->ResetLastSegmentSent ();
  TcpHeader tcpHeader;
  tcpHeader.SetSequenceNumber (m_tcb->m_nextTxSequence);
  tcpHeader.SetAckNumber (m_tcb->m_rxBuffer->NextRxSequence ());
  tcpHeader.SetWindowSize (AdvertisedWindowSize ());
  if (m_endPoint != nullptr)
    {
      tcpHeader.SetSourcePort (m_endPoint->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      tcpHeader.SetSourcePort (m_endPoint6->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (tcpHeader);
  //Send a packet tag for setting ECT bits in IP header
  if (m_tcb->m_ecnState != TcpSocketState::ECN_DISABLED)
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (MarkEcnCodePoint (0, m_tcb->m_ectCodePoint));
      p->AddPacketTag (ipTosTag);

      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (MarkEcnCodePoint (0, m_tcb->m_ectCodePoint));
      p->AddPacketTag (ipTclassTag);
    }
  m_txTrace (p, tcpHeader, this);

  if (m_endPoint != nullptr)
    {
      m_scpstp->SendPacket (p, tcpHeader, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_scpstp->SendPacket (p, tcpHeader, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }

  NS_LOG_LOGIC ("Schedule persist timeout at time "
                << Simulator::Now ().GetSeconds () << " to expire at time "
                << (Simulator::Now () + m_persistTimeout).GetSeconds ());
  m_persistEvent = Simulator::Schedule (m_persistTimeout, &ScpsTpSocketBase::PersistTimeout, this);
}

void
ScpsTpSocketBase::ProcessListen (Ptr<Packet> packet, const TcpHeader& tcpHeader,
                              const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH, URG, CWR and ECE are disregarded.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::CWR | TcpHeader::ECE);

  // Fork a socket if received a SYN. Do nothing otherwise.
  // C.f.: the LISTEN part in tcp_v4_do_rcv() in tcp_ipv4.c in Linux kernel
  if (tcpflags != TcpHeader::SYN)
    {
      return;
    }

  // Call socket's notify function to let the server app know we got a SYN
  // If the server app refuses the connection, do nothing
  if (!NotifyConnectionRequest (fromAddress))
    {
      return;
    }
  // Clone the socket, simulate fork
  Ptr<ScpsTpSocketBase> newSock = ForkScpsTp ();
  NS_LOG_LOGIC ("Cloned a ScpsTpSocketBase " << newSock);
  Simulator::ScheduleNow (&ScpsTpSocketBase::CompleteFork, newSock,
                          packet, tcpHeader, fromAddress, toAddress);
}

int
ScpsTpSocketBase::Send (Ptr<Packet> p, uint32_t flags)
{
  NS_LOG_FUNCTION (this << p);
  NS_ABORT_MSG_IF (flags, "use of flags is not supported in ScpsTpSocketBase::Send()");
  if (m_state == ESTABLISHED || m_state == SYN_SENT || m_state == CLOSE_WAIT)
    {
      // Store the packet into Tx buffer
      if (!m_txBuffer->Add (p))
        { // TxBuffer overflow, send failed
          m_errno = ERROR_MSGSIZE;
          return -1;
        }
      if (m_shutdownSend)
        {
          m_errno = ERROR_SHUTDOWN;
          return -1;
        }

      m_rateOps->CalculateAppLimited(m_tcb->m_cWnd, m_tcb->m_bytesInFlight, m_tcb->m_segmentSize,
                                     m_txBuffer->TailSequence (), m_tcb->m_nextTxSequence,
                                     m_txBuffer->GetLost (), m_txBuffer->GetRetransmitsCount ());

      // Submit the data to lower layers
      NS_LOG_LOGIC ("txBufSize=" << m_txBuffer->Size () << " state " << TcpStateName[m_state]);
      if ((m_state == ESTABLISHED || m_state == CLOSE_WAIT) && AvailableWindow () > 0)
        { // Try to send the data out: Add a little step to allow the application
          // to fill the buffer
          if (!m_sendPendingDataEvent.IsRunning ())
            {
              m_sendPendingDataEvent = Simulator::Schedule (TimeStep (1),
                                                            &ScpsTpSocketBase::SendPendingData,
                                                            this, m_connected);
            }
        }
      return p->GetSize ();
    }
  else
    { // Connection not established yet
      m_errno = ERROR_NOTCONN;
      return -1; // Send failure
    }
}

/* Clean up after Bind. Set up callback functions in the end-point. */
int
ScpsTpSocketBase::SetupCallback (void)
{
  NS_LOG_FUNCTION (this);

  if (m_endPoint == nullptr && m_endPoint6 == nullptr)
    {
      return -1;
    }
  if (m_endPoint != nullptr)
    {
      m_endPoint->SetRxCallback (MakeCallback (&ScpsTpSocketBase::ForwardUp, Ptr<ScpsTpSocketBase> (this)));
      m_endPoint->SetIcmpCallback (MakeCallback (&ScpsTpSocketBase::ForwardIcmp, Ptr<ScpsTpSocketBase> (this)));
      m_endPoint->SetDestroyCallback (MakeCallback (&ScpsTpSocketBase::Destroy, Ptr<ScpsTpSocketBase> (this)));
    }
  if (m_endPoint6 != nullptr)
    {
      m_endPoint6->SetRxCallback (MakeCallback (&ScpsTpSocketBase::ForwardUp6, Ptr<ScpsTpSocketBase> (this)));
      m_endPoint6->SetIcmpCallback (MakeCallback (&ScpsTpSocketBase::ForwardIcmp6, Ptr<ScpsTpSocketBase> (this)));
      m_endPoint6->SetDestroyCallback (MakeCallback (&ScpsTpSocketBase::Destroy6, Ptr<ScpsTpSocketBase> (this)));
    }

  return 0;
}

/* Received a packet upon SYN_SENT */
void
ScpsTpSocketBase::ProcessSynSent (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH and URG are disregarded.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG);

  if (tcpflags == 0)
    { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
      NS_LOG_DEBUG ("SYN_SENT -> ESTABLISHED");
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_OPEN);
      m_tcb->m_congState = TcpSocketState::CA_OPEN;
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_delAckCount = m_delAckMaxCount;
      ReceivedData (packet, tcpHeader);
      Simulator::ScheduleNow (&ScpsTpSocketBase::ConnectionSucceeded, this);
    }
  else if (tcpflags & TcpHeader::ACK && !(tcpflags & TcpHeader::SYN))
    { // Ignore ACK in SYN_SENT
    }
  else if (tcpflags & TcpHeader::SYN && !(tcpflags & TcpHeader::ACK))
    { // Received SYN, move to SYN_RCVD state and respond with SYN+ACK
      NS_LOG_DEBUG ("SYN_SENT -> SYN_RCVD");
      m_state = SYN_RCVD;
      m_synCount = m_synRetries;
      m_tcb->m_rxBuffer->SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));
      /* Check if we received an ECN SYN packet. Change the ECN state of receiver to ECN_IDLE if the traffic is ECN capable and
       * sender has sent ECN SYN packet
       */

      if (m_tcb->m_useEcn != TcpSocketState::Off && (tcpflags & (TcpHeader::CWR | TcpHeader::ECE)) == (TcpHeader::CWR | TcpHeader::ECE))
        {
          NS_LOG_INFO ("Received ECN SYN packet");
          SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK | TcpHeader::ECE);
          NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_IDLE");
          m_tcb->m_ecnState = TcpSocketState::ECN_IDLE;
        }
      else
        {
          m_tcb->m_ecnState = TcpSocketState::ECN_DISABLED;
          SendEmptyPacket (TcpHeader::SYN | TcpHeader::ACK);
        }
    }
  else if (tcpflags & (TcpHeader::SYN | TcpHeader::ACK)
           && m_tcb->m_nextTxSequence + SequenceNumber32 (1) == tcpHeader.GetAckNumber ())
    { // Handshake completed
      NS_LOG_DEBUG ("SYN_SENT -> ESTABLISHED");
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_OPEN);
      m_tcb->m_congState = TcpSocketState::CA_OPEN;
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_tcb->m_rxBuffer->SetNextRxSequence (tcpHeader.GetSequenceNumber () + SequenceNumber32 (1));
      m_tcb->m_highTxMark = ++m_tcb->m_nextTxSequence;
      m_txBuffer->SetHeadSequence (m_tcb->m_nextTxSequence);
      // Before sending packets, update the pacing rate based on RTT measurement so far 
      UpdatePacingRate ();
      SendEmptyPacket (TcpHeader::ACK);

      /* Check if we received an ECN SYN-ACK packet. Change the ECN state of sender to ECN_IDLE if receiver has sent an ECN SYN-ACK
       * packet and the  traffic is ECN Capable
       */
      if (m_tcb->m_useEcn != TcpSocketState::Off && (tcpflags & (TcpHeader::CWR | TcpHeader::ECE)) == (TcpHeader::ECE))
        {
          NS_LOG_INFO ("Received ECN SYN-ACK packet.");
          NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_IDLE");
          m_tcb->m_ecnState = TcpSocketState::ECN_IDLE;
        }
      else
        {
          m_tcb->m_ecnState = TcpSocketState::ECN_DISABLED;
        }
      SendPendingData (m_connected);
      Simulator::ScheduleNow (&ScpsTpSocketBase::ConnectionSucceeded, this);
      // Always respond to first data packet to speed up the connection.
      // Remove to get the behaviour of old NS-3 code.
      m_delAckCount = m_delAckMaxCount;
    }
  else
    { // Other in-sequence input
      if (!(tcpflags & TcpHeader::RST))
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag combination " << TcpHeader::FlagsToString (tcpHeader.GetFlags ()) <<
                        " received in SYN_SENT. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a in-sequence FIN. Close down this socket. */
void
ScpsTpSocketBase::DoPeerClose (void)
{
  NS_ASSERT (m_state == ESTABLISHED || m_state == SYN_RCVD ||
             m_state == FIN_WAIT_1 || m_state == FIN_WAIT_2);

  // Move the state to CLOSE_WAIT
  NS_LOG_DEBUG (TcpStateName[m_state] << " -> CLOSE_WAIT");
  m_state = CLOSE_WAIT;

  if (!m_closeNotified)
    {
      // The normal behaviour for an application is that, when the peer sent a in-sequence
      // FIN, the app should prepare to close. The app has two choices at this point: either
      // respond with ShutdownSend() call to declare that it has nothing more to send and
      // the socket can be closed immediately; or remember the peer's close request, wait
      // until all its existing data are pushed into the TCP socket, then call Close()
      // explicitly.
      NS_LOG_LOGIC ("TCP " << this << " calling NotifyNormalClose");
      NotifyNormalClose ();
      m_closeNotified = true;
    }
  if (m_shutdownSend)
    { // The application declares that it would not sent any more, close this socket
      Close ();
    }
  else
    { // Need to ack, the application will close later
      SendEmptyPacket (TcpHeader::ACK);
    }
  if (m_state == LAST_ACK)
    {
      m_dataRetrCount = m_dataRetries; // prevent endless FINs
      NS_LOG_LOGIC ("ScpsTpSocketBase " << this << " scheduling LATO1");
      Time lastRto = m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4);
      m_lastAckEvent = Simulator::Schedule (lastRto, &ScpsTpSocketBase::LastAckTimeout, this);
    }
}

uint16_t
ScpsTpSocketBase::AdvertisedWindowSize (bool scale) const
{
  NS_LOG_FUNCTION (this << scale);
  uint32_t w;

  // We don't want to advertise 0 after a FIN is received. So, we just use
  // the previous value of the advWnd.
  if (m_tcb->m_rxBuffer->GotFin ())
    {
      w = m_advWnd;
    }
  else
    {
      NS_ASSERT_MSG (m_tcb->m_rxBuffer->MaxRxSequence () - m_tcb->m_rxBuffer->NextRxSequence () >= 0,
                     "Unexpected sequence number values");
      w = static_cast<uint32_t> (m_tcb->m_rxBuffer->MaxRxSequence () - m_tcb->m_rxBuffer->NextRxSequence ());
    }

  // Ugly, but we are not modifying the state, that variable
  // is used only for tracing purpose.
  if (w != m_advWnd)
    {
      const_cast<ScpsTpSocketBase*> (this)->m_advWnd = w;
    }
  if (scale)
    {
      w >>= m_rcvWindShift;
    }
  if (w > m_maxWinSize)
    {
      w = m_maxWinSize;
      NS_LOG_WARN ("Adv window size truncated to " << m_maxWinSize << "; possibly to avoid overflow of the 16-bit integer");
    }
  NS_LOG_LOGIC ("Returning AdvertisedWindowSize of " << static_cast<uint16_t> (w));
  return static_cast<uint16_t> (w);
}

// Receipt of new packet, put into Rx buffer
void
ScpsTpSocketBase::ReceivedData (Ptr<Packet> p, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);
  NS_LOG_DEBUG ("Data segment, seq=" << tcpHeader.GetSequenceNumber () <<
                " pkt size=" << p->GetSize () );

  // Put into Rx buffer
  SequenceNumber32 expectedSeq = m_tcb->m_rxBuffer->NextRxSequence ();
  NS_ASSERT (m_tcb->m_rxBuffer->GetInstanceTypeId () == ScpsTpRxBuffer::GetTypeId ());
  if (!m_tcb->m_rxBuffer->Add (p, tcpHeader))
    { // Insert failed: No data or RX buffer full
      if (m_tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || m_tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
        {
          SendEmptyPacket (TcpHeader::ACK | TcpHeader::ECE);
          NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_SENDING_ECE");
          m_tcb->m_ecnState = TcpSocketState::ECN_SENDING_ECE;
        }
      else
        {
          SendEmptyPacket (TcpHeader::ACK);
        }
      return;
    }
  // Notify app to receive if necessary
  if (expectedSeq < m_tcb->m_rxBuffer->NextRxSequence ())
    { // NextRxSeq advanced, we have something to send to the app
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
      // Handle exceptions
      if (m_closeNotified)
        {
          NS_LOG_WARN ("Why TCP " << this << " got data after close notification?");
        }
      // If we received FIN before and now completed all "holes" in rx buffer,
      // invoke peer close procedure
      if (m_tcb->m_rxBuffer->Finished () && (tcpHeader.GetFlags () & TcpHeader::FIN) == 0)
        {
          DoPeerClose ();
          return;
        }
    }
  // Now send a new ACK packet acknowledging all received and delivered data
  if (m_tcb->m_rxBuffer->Size () > m_tcb->m_rxBuffer->Available () || m_tcb->m_rxBuffer->NextRxSequence () > expectedSeq + p->GetSize ())
    { // A gap exists in the buffer, or we filled a gap: Always ACK
      m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_NON_DELAYED_ACK);
      if (m_tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || m_tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
        {
          SendEmptyPacket (TcpHeader::ACK | TcpHeader::ECE);
          NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_SENDING_ECE");
          m_tcb->m_ecnState = TcpSocketState::ECN_SENDING_ECE;
        }
      else
        {
          SendEmptyPacket (TcpHeader::ACK);
        }
    }
  else
    { // In-sequence packet: ACK if delayed ack count allows
      if (++m_delAckCount >= m_delAckMaxCount)
        {
          m_delAckEvent.Cancel ();
          m_delAckCount = 0;
          m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_NON_DELAYED_ACK);
          if (m_tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || m_tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
            {
              NS_LOG_DEBUG("Congestion algo " << m_congestionControl->GetName ());
              SendEmptyPacket (TcpHeader::ACK | TcpHeader::ECE);
              NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_SENDING_ECE");
              m_tcb->m_ecnState = TcpSocketState::ECN_SENDING_ECE;
            }
          else
            {
              SendEmptyPacket (TcpHeader::ACK);
            }
        }
      else if (!m_delAckEvent.IsExpired ())
        {
          m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_DELAYED_ACK);
        }
      else if (m_delAckEvent.IsExpired ())
        {
          m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_DELAYED_ACK);
          m_delAckEvent = Simulator::Schedule (m_delAckTimeout,
                                               &ScpsTpSocketBase::DelAckTimeout, this);
          NS_LOG_LOGIC (this << " scheduled delayed ACK at " <<
                        (Simulator::Now () + Simulator::GetDelayLeft (m_delAckEvent)).GetSeconds ());
        }
    }
}

// Called by the ReceivedAck() when new ACK received and by ProcessSynRcvd()
// when the three-way handshake completed. This cancels retransmission timer
// and advances Tx window
void
ScpsTpSocketBase::NewAck (SequenceNumber32 const& ack, bool resetRTO)
{
  NS_LOG_FUNCTION (this << ack);

  // Reset the data retransmission count. We got a new ACK!
  m_dataRetrCount = m_dataRetries;

  if (m_state != SYN_RCVD && resetRTO)
    { // Set RTO unless the ACK is received in SYN_RCVD state
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      // On receiving a "New" ack we restart retransmission timer .. RFC 6298
      // RFC 6298, clause 2.4
      m_rto = Max (m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4), m_minRto);

      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &ScpsTpSocketBase::ReTxTimeout, this);
    }

  // Note the highest ACK and tell app to send more
  NS_LOG_LOGIC ("TCP " << this << " NewAck " << ack <<
                " numberAck " << (ack - m_txBuffer->HeadSequence ())); // Number bytes ack'ed

  if (GetTxAvailable () > 0)
    {
      NotifySend (GetTxAvailable ());
    }
  if (ack > m_tcb->m_nextTxSequence)
    {
      m_tcb->m_nextTxSequence = ack; // If advanced
    }
  if (m_txBuffer->Size () == 0 && m_state != FIN_WAIT_1 && m_state != CLOSING)
    { // No retransmit timer if no data to retransmit
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
    }
}

void
ScpsTpSocketBase::LastAckTimeout (void)
{
  NS_LOG_FUNCTION (this);

  m_lastAckEvent.Cancel ();
  if (m_state == LAST_ACK)
    {
      if (m_dataRetrCount == 0)
        {
          NS_LOG_INFO ("LAST-ACK: No more data retries available. Dropping connection");
          NotifyErrorClose ();
          DeallocateEndPoint ();
          return;
        }
      m_dataRetrCount--;
      SendEmptyPacket (TcpHeader::FIN | TcpHeader::ACK);
      NS_LOG_LOGIC ("ScpsTpSocketBase " << this << " rescheduling LATO1");
      Time lastRto = m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4);
      m_lastAckEvent = Simulator::Schedule (lastRto, &ScpsTpSocketBase::LastAckTimeout, this);
    }
}

/* Move TCP to Time_Wait state and schedule a transition to Closed state */
void
ScpsTpSocketBase::TimeWait ()
{
  NS_LOG_DEBUG (TcpStateName[m_state] << " -> TIME_WAIT");
  m_state = TIME_WAIT;
  CancelAllTimers ();
  if (!m_closeNotified)
    {
      // Technically the connection is not fully closed, but we notify now
      // because an implementation (real socket) would behave as if closed.
      // Notify normal close when entering TIME_WAIT or leaving LAST_ACK.
      NotifyNormalClose ();
      m_closeNotified = true;
    }
  // Move from TIME_WAIT to CLOSED after 2*MSL. Max segment lifetime is 2 min
  // according to RFC793, p.28
  m_timewaitEvent = Simulator::Schedule (Seconds (2 * m_msl),
                                         &ScpsTpSocketBase::CloseAndNotify, this);
}

Ptr<ScpsTpSocketBase>
ScpsTpSocketBase::ForkScpsTp (void)
{
  return CopyObject<ScpsTpSocketBase> (this);
}

// Sender should reduce the Congestion Window as a response to receiver's
// ECN Echo notification only once per window
void
ScpsTpSocketBase::EnterCwr (uint32_t currentDelivered)
{
  NS_LOG_FUNCTION (this << currentDelivered);
  m_tcb->m_ssThresh = m_congestionControl->GetSsThresh (m_tcb, BytesInFlight ());
  NS_LOG_DEBUG ("Reduce ssThresh to " << m_tcb->m_ssThresh);
  // Do not update m_cWnd, under assumption that recovery process will
  // gradually bring it down to m_ssThresh.  Update the 'inflated' value of
  // cWnd used for tracing, however.
  m_tcb->m_cWndInfl = m_tcb->m_ssThresh;
  NS_ASSERT (m_tcb->m_congState != TcpSocketState::CA_CWR);
  NS_LOG_DEBUG (TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> CA_CWR");
  m_tcb->m_congState = TcpSocketState::CA_CWR;
  // CWR state will be exited when the ack exceeds the m_recover variable.
  // Do not set m_recoverActive (which applies to a loss-based recovery)
  // m_recover corresponds to Linux tp->high_seq
  m_recover = m_tcb->m_highTxMark;
  if (!m_congestionControl->HasCongControl ())
    {
      // If there is a recovery algorithm, invoke it.
      m_recoveryOps->EnterRecovery (m_tcb, m_dupAckCount, UnAckDataCount (), currentDelivered);
      NS_LOG_INFO ("Enter CWR recovery mode; set cwnd to " << m_tcb->m_cWnd
                    << ", ssthresh to " << m_tcb->m_ssThresh
                    << ", recover to " << m_recover);
    }
}

/* Process the newly received ACK */
void
ScpsTpSocketBase::ReceivedAck (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  NS_ASSERT (0 != (tcpHeader.GetFlags () & TcpHeader::ACK));
  NS_ASSERT (m_tcb->m_segmentSize > 0);

  uint32_t previousLost = m_txBuffer->GetLost ();
  uint32_t priorInFlight = m_tcb->m_bytesInFlight.Get ();

  // RFC 6675, Section 5, 1st paragraph:
  // Upon the receipt of any ACK containing SACK information, the
  // scoreboard MUST be updated via the Update () routine (done in ReadOptions)
  uint32_t bytesSacked = 0;
  uint64_t previousDelivered = m_rateOps->GetConnectionRate ().m_delivered;
  ReadOptions (tcpHeader, &bytesSacked);

  SequenceNumber32 ackNumber = tcpHeader.GetAckNumber ();
  SequenceNumber32 oldHeadSequence = m_txBuffer->HeadSequence ();

  if (ackNumber < oldHeadSequence)
    {
      NS_LOG_DEBUG ("Possibly received a stale ACK (ack number < head sequence)");
      // If there is any data piggybacked, store it into m_rxBuffer
      if (packet->GetSize () > 0)
        {
          ReceivedData (packet, tcpHeader);
        }
      return;
    }
  if ((ackNumber > oldHeadSequence) && (ackNumber < m_recover)
                                    && (m_tcb->m_congState == TcpSocketState::CA_RECOVERY))
    {
      uint32_t segAcked = (ackNumber - oldHeadSequence)/m_tcb->m_segmentSize;
      for (uint32_t i = 0; i < segAcked; i++)
        {
          if (m_txBuffer->IsRetransmittedDataAcked (ackNumber - (i * m_tcb->m_segmentSize)))
            {
              m_tcb->m_isRetransDataAcked = true;
              NS_LOG_DEBUG ("Ack Number " << ackNumber <<
                            "is ACK of retransmitted packet.");
            }
        }
    }

  m_txBuffer->DiscardUpTo (ackNumber, MakeCallback (&TcpRateOps::SkbDelivered, m_rateOps));

  uint32_t currentDelivered = static_cast<uint32_t> (m_rateOps->GetConnectionRate ().m_delivered - previousDelivered);
  m_tcb->m_lastAckedSackedBytes = currentDelivered;

  if (m_tcb->m_congState == TcpSocketState::CA_CWR && (ackNumber > m_recover))
    {
      // Recovery is over after the window exceeds m_recover
      // (although it may be re-entered below if ECE is still set)
      NS_LOG_DEBUG (TcpSocketState::TcpCongStateName[m_tcb->m_congState] << " -> CA_OPEN");
      m_tcb->m_congState = TcpSocketState::CA_OPEN;
      if (!m_congestionControl->HasCongControl ())
        {
           m_tcb->m_cWnd = m_tcb->m_ssThresh.Get ();
           m_recoveryOps->ExitRecovery (m_tcb);
           m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_COMPLETE_CWR);
        }
    }

  if (ackNumber > oldHeadSequence && (m_tcb->m_ecnState != TcpSocketState::ECN_DISABLED) && (tcpHeader.GetFlags () & TcpHeader::ECE))
    {
      if (m_ecnEchoSeq < ackNumber)
        {
          NS_LOG_INFO ("Received ECN Echo is valid");
          m_ecnEchoSeq = ackNumber;
          NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_ECE_RCVD");
          //Set lossType to congestion
          SetLossType (ScpsTpSocketBase::Congestion);
          m_tcb->m_ecnState = TcpSocketState::ECN_ECE_RCVD;
          if (m_tcb->m_congState != TcpSocketState::CA_CWR)
            {
              EnterCwr (currentDelivered);
            }
        }
    }
  else if (m_tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD && !(tcpHeader.GetFlags () & TcpHeader::ECE))
    {
      m_tcb->m_ecnState = TcpSocketState::ECN_IDLE;
      //Set lossType to corruption(默认值)
      SetLossType (ScpsTpSocketBase::Corruption);
    }

  // Update bytes in flight before processing the ACK for proper calculation of congestion window
  NS_LOG_INFO ("Update bytes in flight before processing the ACK.");
  BytesInFlight ();

  // RFC 6675 Section 5: 2nd, 3rd paragraph and point (A), (B) implementation
  // are inside the function ProcessAck
  ProcessAck (ackNumber, (bytesSacked > 0), currentDelivered, oldHeadSequence);
  m_tcb->m_isRetransDataAcked = false;

  if (m_congestionControl->HasCongControl ())
    {
      uint32_t currentLost = m_txBuffer->GetLost ();
      uint32_t lost = (currentLost > previousLost) ?
            currentLost - previousLost :
            previousLost - currentLost;
      auto rateSample = m_rateOps->GenerateSample (currentDelivered, lost,
                                              false, priorInFlight, m_tcb->m_minRtt);
      auto rateConn = m_rateOps->GetConnectionRate ();
      m_congestionControl->CongControl(m_tcb, rateConn, rateSample);
    }

  // If there is any data piggybacked, store it into m_rxBuffer
  if (packet->GetSize () > 0)
    {
      ReceivedData (packet, tcpHeader);
    }

  // RFC 6675, Section 5, point (C), try to send more data. NB: (C) is implemented
  // inside SendPendingData
  SendPendingData (m_connected);
}


// Retransmit timeout
void
ScpsTpSocketBase::ReTxTimeout ()
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }

  if (m_state == SYN_SENT)
    {
      NS_ASSERT (m_synCount > 0);
      if (m_tcb->m_useEcn == TcpSocketState::On)
        {
          SendEmptyPacket (TcpHeader::SYN | TcpHeader::ECE | TcpHeader::CWR);
        }
      else
        {
          SendEmptyPacket (TcpHeader::SYN);
        }
      return;
    }

  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  if (m_txBuffer->Size () == 0)
    {
      if (m_state == FIN_WAIT_1 || m_state == CLOSING)
        { // Must have lost FIN, re-send
          SendEmptyPacket (TcpHeader::FIN);
        }
      return;
    }

  NS_LOG_DEBUG ("Checking if Connection is Established");
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_tcb->m_highTxMark && m_txBuffer->Size () == 0)
    {
      NS_LOG_DEBUG ("Already Sent full data" << m_txBuffer->HeadSequence () << " " << m_tcb->m_highTxMark);
      return;
    }

  if (m_dataRetrCount == 0)
    {
      NS_LOG_INFO ("No more data retries available. Dropping connection");
      NotifyErrorClose ();
      DeallocateEndPoint ();
      return;
    }
  else
    {
      --m_dataRetrCount;
    }

  uint32_t inFlightBeforeRto = BytesInFlight ();
  bool resetSack = !m_sackEnabled; // Reset SACK information if SACK is not enabled.
                                   // The information in the ScpsTpTxBuffer is guessed, in this case.

  // Reset dupAckCount
  m_dupAckCount = 0;
  if (!m_sackEnabled)
    {
      m_txBuffer->ResetRenoSack ();
    }

  // From RFC 6675, Section 5.1
  // [RFC2018] suggests that a TCP sender SHOULD expunge the SACK
  // information gathered from a receiver upon a retransmission timeout
  // (RTO) "since the timeout might indicate that the data receiver has
  // reneged."  Additionally, a TCP sender MUST "ignore prior SACK
  // information in determining which data to retransmit."
  // It has been suggested that, as long as robust tests for
  // reneging are present, an implementation can retain and use SACK
  // information across a timeout event [Errata1610].
  // The head of the sent list will not be marked as sacked, therefore
  // will be retransmitted, if the receiver renegotiate the SACK blocks
  // that we received.
  m_txBuffer->SetSentListLost (resetSack);

  // From RFC 6675, Section 5.1
  // If an RTO occurs during loss recovery as specified in this document,
  // RecoveryPoint MUST be set to HighData.  Further, the new value of
  // RecoveryPoint MUST be preserved and the loss recovery algorithm
  // outlined in this document MUST be terminated.
  m_recover = m_tcb->m_highTxMark;
  m_recoverActive = true;


  // 如果m_losstype为congestion,则倍增RTO，调整cwnd和ssthresh，如果是corruption则直接重传数据
  if(m_lossType == ScpsTpSocketBase::Congestion)
    {
      NS_LOG_DEBUG ("超时原因是congestion");
      // RFC 6298, clause 2.5, double the timer
      Time doubledRto = m_rto + m_rto;
      m_rto = Min (doubledRto, Time::FromDouble (60,  Time::S));

      // Empty RTT history
      m_history.clear ();

      // Please don't reset highTxMark, it is used for retransmission detection

      // When a TCP sender detects segment loss using the retransmission timer
      // and the given segment has not yet been resent by way of the
      // retransmission timer, decrease ssThresh
      if (m_tcb->m_congState != TcpSocketState::CA_LOSS || !m_txBuffer->IsHeadRetransmitted ())//m_txBuffer->IsHeadRetransmitted确保一个RTO只减小一次ssThresh
        {
          m_tcb->m_ssThresh = m_congestionControl->GetSsThresh (m_tcb, inFlightBeforeRto);
        }

      // Cwnd set to 1 MSS 进入慢启动
      m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_LOSS);
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_LOSS);
      m_tcb->m_congState = TcpSocketState::CA_LOSS;
      m_tcb->m_cWnd = m_tcb->m_segmentSize;
      m_tcb->m_cWndInfl = m_tcb->m_cWnd;

      m_pacingTimer.Cancel ();

      NS_LOG_DEBUG ("RTO. Reset cwnd to " <<  m_tcb->m_cWnd << ", ssthresh to " <<
                    m_tcb->m_ssThresh << ", restart from seqnum " <<
                    m_txBuffer->HeadSequence () << " doubled rto to " <<
                    m_rto.Get ().GetSeconds () << " s");

      NS_ASSERT_MSG (BytesInFlight () == 0, "There are some bytes in flight after an RTO: " <<
                    BytesInFlight ());

      SendPendingData (m_connected);

      NS_ASSERT_MSG (BytesInFlight () <= m_tcb->m_segmentSize,
                    "In flight (" << BytesInFlight () <<
                    ") there is more than one segment (" << m_tcb->m_segmentSize << ")");
    }
  else if(m_lossType == ScpsTpSocketBase::Corruption)
    { //
      NS_LOG_DEBUG ("超时原因是conrruption");
      // Empty RTT history
      m_history.clear ();

      //进入CA_LOSS状态，但不重置cwnd和ssthresh
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_LOSS);
      m_tcb->m_congState = TcpSocketState::CA_LOSS;

      m_pacingTimer.Cancel ();

      NS_ASSERT_MSG (BytesInFlight () == 0, "There are some bytes in flight after an RTO: " <<
                    BytesInFlight ());

      SendPendingData (m_connected);

      //因为此时并为重置cwnd和ssthresh，所以SendPendingData会导致重传后的BytesInFlight大于1个MSS
    }
}

void
ScpsTpSocketBase::EnterRecovery (uint32_t currentDelivered)
{
  NS_LOG_FUNCTION (this);
  NS_ASSERT (m_tcb->m_congState != TcpSocketState::CA_RECOVERY);

  NS_LOG_DEBUG (TcpSocketState::TcpCongStateName[m_tcb->m_congState] <<
                " -> CA_RECOVERY");

  if (!m_sackEnabled)
    {
      // One segment has left the network, PLUS the head is lost
      m_txBuffer->AddRenoSack ();
      m_txBuffer->MarkHeadAsLost ();
    }
  else
    {
      if (!m_txBuffer->IsLost (m_txBuffer->HeadSequence ()))
        {
          // We received 3 dupacks, but the head is not marked as lost
          // (received less than 3 SACK block ahead).
          // Manually set it as lost.
          m_txBuffer->MarkHeadAsLost ();
        }
    }

  // RFC 6675, point (4):
  // (4) Invoke fast retransmit and enter loss recovery as follows:
  // (4.1) RecoveryPoint = HighData
  m_recover = m_tcb->m_highTxMark;
  m_recoverActive = true;
  


  //当m_losstype为congestion时，重置cwnd和ssthresh,之后再重传数据，当m_losstype为corruption时，直接重传数据
  if(m_lossType == ScpsTpSocketBase::Congestion)
    {
      m_isCorruptionRecovery = false;
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_RECOVERY);
      m_tcb->m_congState = TcpSocketState::CA_RECOVERY;
      // (4.2) ssthresh = cwnd = (FlightSize / 2)
      // If SACK is not enabled, still consider the head as 'in flight' for
      // compatibility with old ns-3 versions
      uint32_t bytesInFlight = m_sackEnabled ? BytesInFlight () : BytesInFlight () + m_tcb->m_segmentSize;
      m_tcb->m_ssThresh = m_congestionControl->GetSsThresh (m_tcb, bytesInFlight);
      if (!m_congestionControl->HasCongControl ())
        {
          m_recoveryOps->EnterRecovery (m_tcb, m_dupAckCount, UnAckDataCount (), currentDelivered);
          NS_LOG_INFO (m_dupAckCount << " dupack. Enter fast recovery mode." <<
                      "Reset cwnd to " << m_tcb->m_cWnd << ", ssthresh to " <<
                      m_tcb->m_ssThresh << " at fast recovery seqnum " << m_recover <<
                      " calculated in flight: " << bytesInFlight);
        }
      // (4.3) Retransmit the first data segment presumed dropped
      DoRetransmit ();
      // (4.4) Run SetPipe ()
      // (4.5) Proceed to step (C)
      // these steps are done after the ProcessAck function (SendPendingData)
    }
  else if(m_lossType == ScpsTpSocketBase::Corruption)
    {
      m_isCorruptionRecovery = true;
      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_RECOVERY);
      m_tcb->m_congState = TcpSocketState::CA_RECOVERY;
      // (4.2) ssthresh = cwnd = (FlightSize / 2)
      // If SACK is not enabled, still consider the head as 'in flight' for
      // compatibility with old ns-3 versions
      // (4.3) Retransmit the first data segment presumed dropped
      DoRetransmit ();
      // (4.4) Run SetPipe ()
      // (4.5) Proceed to step (C)
      // these steps are done after the ProcessAck function (SendPendingData)
    }
 
}

void
ScpsTpSocketBase::ProcessAck(const SequenceNumber32 &ackNumber, bool scoreboardUpdated,
                          uint32_t currentDelivered, const SequenceNumber32 &oldHeadSequence)
{
  NS_LOG_FUNCTION (this << ackNumber << scoreboardUpdated);
  // RFC 6675, Section 5, 2nd paragraph:
  // If the incoming ACK is a cumulative acknowledgment, the TCP MUST
  // reset DupAcks to zero.
  bool exitedFastRecovery = false;
  uint32_t oldDupAckCount = m_dupAckCount; // remember the old value
  m_tcb->m_lastAckedSeq = ackNumber; // Update lastAckedSeq
  uint32_t bytesAcked = 0;

  /* In RFC 5681 the definition of duplicate acknowledgment was strict:
   *
   * (a) the receiver of the ACK has outstanding data,
   * (b) the incoming acknowledgment carries no data,
   * (c) the SYN and FIN bits are both off,
   * (d) the acknowledgment number is equal to the greatest acknowledgment
   *     received on the given connection (TCP.UNA from [RFC793]),
   * (e) the advertised window in the incoming acknowledgment equals the
   *     advertised window in the last incoming acknowledgment.
   *
   * With RFC 6675, this definition has been reduced:
   *
   * (a) the ACK is carrying a SACK block that identifies previously
   *     unacknowledged and un-SACKed octets between HighACK (TCP.UNA) and
   *     HighData (m_highTxMark)
   */

  bool isDupack = m_sackEnabled ?
    scoreboardUpdated
    : ackNumber == oldHeadSequence &&
    ackNumber < m_tcb->m_highTxMark;

  NS_LOG_DEBUG ("ACK of " << ackNumber <<
                " SND.UNA=" << oldHeadSequence <<
                " SND.NXT=" << m_tcb->m_nextTxSequence <<
                " in state: " << TcpSocketState::TcpCongStateName[m_tcb->m_congState] <<
                " with m_recover: " << m_recover);

  // RFC 6675, Section 5, 3rd paragraph:
  // If the incoming ACK is a duplicate acknowledgment per the definition
  // in Section 2 (regardless of its status as a cumulative
  // acknowledgment), and the TCP is not currently in loss recovery
  if (isDupack)
    {
      // loss recovery check is done inside this function thanks to
      // the congestion state machine
      DupAck (currentDelivered);
    }

  if (ackNumber == oldHeadSequence
      && ackNumber == m_tcb->m_highTxMark)
    {
      // Dupack, but the ACK is precisely equal to the nextTxSequence
      return;
    }
  else if (ackNumber == oldHeadSequence
           && ackNumber > m_tcb->m_highTxMark)
    {
      // ACK of the FIN bit ... nextTxSequence is not updated since we
      // don't have anything to transmit
      NS_LOG_DEBUG ("Update nextTxSequence manually to " << ackNumber);
      m_tcb->m_nextTxSequence = ackNumber;
    }
  else if (ackNumber == oldHeadSequence)
    {
      // DupAck. Artificially call PktsAcked: after all, one segment has been ACKed.
      m_congestionControl->PktsAcked (m_tcb, 1, m_tcb->m_lastRtt);
    }
  else if (ackNumber > oldHeadSequence)
    {
      // Please remember that, with SACK, we can enter here even if we
      // received a dupack.
      bytesAcked = ackNumber - oldHeadSequence;
      uint32_t segsAcked  = bytesAcked / m_tcb->m_segmentSize;
      m_bytesAckedNotProcessed += bytesAcked % m_tcb->m_segmentSize;
      bytesAcked -= bytesAcked % m_tcb->m_segmentSize;

      if (m_bytesAckedNotProcessed >= m_tcb->m_segmentSize)
        {
          segsAcked += 1;
          bytesAcked += m_tcb->m_segmentSize;
          m_bytesAckedNotProcessed -= m_tcb->m_segmentSize;
        }

      // Dupack count is reset to eventually fast-retransmit after 3 dupacks.
      // Any SACK-ed segment will be cleaned up by DiscardUpTo.
      // In the case that we advanced SND.UNA, but the ack contains SACK blocks,
      // we do not reset. At the third one we will retransmit.
      // If we are already in recovery, this check is useless since dupAcks
      // are not considered in this phase. When from Recovery we go back
      // to open, then dupAckCount is reset anyway.
      if (!isDupack)
        {
          m_dupAckCount = 0;
        }

      // RFC 6675, Section 5, part (B)
      // (B) Upon receipt of an ACK that does not cover RecoveryPoint, the
      // following actions MUST be taken:
      //
      // (B.1) Use Update () to record the new SACK information conveyed
      //       by the incoming ACK.
      // (B.2) Use SetPipe () to re-calculate the number of octets still
      //       in the network.
      //
      // (B.1) is done at the beginning, while (B.2) is delayed to part (C) while
      // trying to transmit with SendPendingData. We are not allowed to exit
      // the CA_RECOVERY phase. Just process this partial ack (RFC 5681)
      if (ackNumber < m_recover && m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
        {
          if (!m_sackEnabled)
            {
              // Manually set the head as lost, it will be retransmitted.
              NS_LOG_INFO ("Partial ACK. Manually setting head as lost");
              m_txBuffer->MarkHeadAsLost ();
            }

          // Before retransmitting the packet perform DoRecovery and check if
          // there is available window
          if (!m_congestionControl->HasCongControl () && segsAcked >= 1)
            {
              m_recoveryOps->DoRecovery (m_tcb, currentDelivered);
            }

          // If the packet is already retransmitted do not retransmit it
          if (!m_txBuffer->IsRetransmittedDataAcked (ackNumber + m_tcb->m_segmentSize))
            {
              DoRetransmit (); // Assume the next seq is lost. Retransmit lost packet
              m_tcb->m_cWndInfl = SafeSubtraction (m_tcb->m_cWndInfl, bytesAcked);
            }

          // This partial ACK acknowledge the fact that one segment has been
          // previously lost and now successfully received. All others have
          // been processed when they come under the form of dupACKs
          m_congestionControl->PktsAcked (m_tcb, 1, m_tcb->m_lastRtt);
          NewAck (ackNumber, m_isFirstPartialAck);

          if (m_isFirstPartialAck)
            {
              NS_LOG_DEBUG ("Partial ACK of " << ackNumber <<
                            " and this is the first (RTO will be reset);"
                            " cwnd set to " << m_tcb->m_cWnd <<
                            " recover seq: " << m_recover <<
                            " dupAck count: " << m_dupAckCount);
              m_isFirstPartialAck = false;
            }
          else
            {
              NS_LOG_DEBUG ("Partial ACK of " << ackNumber <<
                            " and this is NOT the first (RTO will not be reset)"
                            " cwnd set to " << m_tcb->m_cWnd <<
                            " recover seq: " << m_recover <<
                            " dupAck count: " << m_dupAckCount);
            }
        }
      // From RFC 6675 section 5.1
      // In addition, a new recovery phase (as described in Section 5) MUST NOT
      // be initiated until HighACK is greater than or equal to the new value
      // of RecoveryPoint.
      else if (ackNumber < m_recover && m_tcb->m_congState == TcpSocketState::CA_LOSS)
        {
          m_congestionControl->PktsAcked (m_tcb, segsAcked, m_tcb->m_lastRtt);
          m_congestionControl->IncreaseWindow (m_tcb, segsAcked);

          NS_LOG_DEBUG (" Cong Control Called, cWnd=" << m_tcb->m_cWnd <<
                        " ssTh=" << m_tcb->m_ssThresh);
          if (!m_sackEnabled)
            {
              NS_ASSERT_MSG (m_txBuffer->GetSacked () == 0,
                             "Some segment got dup-acked in CA_LOSS state: " <<
                             m_txBuffer->GetSacked ());
            }
          NewAck (ackNumber, true);
        }
      else if (m_tcb->m_congState == TcpSocketState::CA_CWR)
        {
          m_congestionControl->PktsAcked (m_tcb, segsAcked, m_tcb->m_lastRtt);
          // TODO: need to check behavior if marking is compounded by loss
          // and/or packet reordering
          if (!m_congestionControl->HasCongControl () && segsAcked >= 1)
            {
              m_recoveryOps->DoRecovery (m_tcb, currentDelivered);
            }
          NewAck (ackNumber, true);
        }
      else
        {
          if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
            {
              m_congestionControl->PktsAcked (m_tcb, segsAcked, m_tcb->m_lastRtt);
            }
          else if (m_tcb->m_congState == TcpSocketState::CA_DISORDER)
            {
              if (segsAcked >= oldDupAckCount)
                {
                  m_congestionControl->PktsAcked (m_tcb, segsAcked - oldDupAckCount, m_tcb->m_lastRtt);
                }

              if (!isDupack)
                {
                  // The network reorder packets. Linux changes the counting lost
                  // packet algorithm from FACK to NewReno. We simply go back in Open.
                  m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_OPEN);
                  m_tcb->m_congState = TcpSocketState::CA_OPEN;
                  NS_LOG_DEBUG (segsAcked << " segments acked in CA_DISORDER, ack of " <<
                                ackNumber << " exiting CA_DISORDER -> CA_OPEN");
                }
              else
                {
                  NS_LOG_DEBUG (segsAcked << " segments acked in CA_DISORDER, ack of " <<
                                ackNumber << " but still in CA_DISORDER");
                }
            }
          // RFC 6675, Section 5:
          // Once a TCP is in the loss recovery phase, the following procedure
          // MUST be used for each arriving ACK:
          // (A) An incoming cumulative ACK for a sequence number greater than
          // RecoveryPoint signals the end of loss recovery, and the loss
          // recovery phase MUST be terminated.  Any information contained in
          // the scoreboard for sequence numbers greater than the new value of
          // HighACK SHOULD NOT be cleared when leaving the loss recovery
          // phase.
          else if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
            {
              m_isFirstPartialAck = true;

              // Recalculate the segs acked, that are from m_recover to ackNumber
              // (which are the ones we have not passed to PktsAcked and that
              // can increase cWnd)
              // TODO:  check consistency for dynamic segment size
              segsAcked = static_cast<uint32_t>(ackNumber - oldHeadSequence) / m_tcb->m_segmentSize;
              m_congestionControl->PktsAcked (m_tcb, segsAcked, m_tcb->m_lastRtt);
              m_congestionControl->CwndEvent (m_tcb, TcpSocketState::CA_EVENT_COMPLETE_CWR);
              m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_OPEN);
              m_tcb->m_congState = TcpSocketState::CA_OPEN;
              exitedFastRecovery = true;
              m_dupAckCount = 0; // From recovery to open, reset dupack

              NS_LOG_DEBUG (segsAcked << " segments acked in CA_RECOVER, ack of " <<
                            ackNumber << ", exiting CA_RECOVERY -> CA_OPEN");
            }
          else if (m_tcb->m_congState == TcpSocketState::CA_LOSS)
            {
              m_isFirstPartialAck = true;

              // Recalculate the segs acked, that are from m_recover to ackNumber
              // (which are the ones we have not passed to PktsAcked and that
              // can increase cWnd)
              segsAcked = (ackNumber - m_recover) / m_tcb->m_segmentSize;

              m_congestionControl->PktsAcked (m_tcb, segsAcked, m_tcb->m_lastRtt);

              m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_OPEN);
              m_tcb->m_congState = TcpSocketState::CA_OPEN;
              NS_LOG_DEBUG (segsAcked << " segments acked in CA_LOSS, ack of" <<
                            ackNumber << ", exiting CA_LOSS -> CA_OPEN");
            }

          if (ackNumber >= m_recover)
            {
              // All lost segments in the congestion event have been
              // retransmitted successfully. The recovery point (m_recover)
              // should be deactivated.
              m_recoverActive = false;
            }

          if (exitedFastRecovery)
            {
              NewAck (ackNumber, true);
              //如果导致进入CA_RECOVERY的原因是corruption，
              //则在退出recovery时，不调用拥塞控制算法修改ssthresh和cwnd
              //如果原因是congestion，则调用拥塞控制算法修改ssthresh和cwnd
              if(!m_isCorruptionRecovery)
              {
                m_tcb->m_cWnd = m_tcb->m_ssThresh.Get ();
                m_recoveryOps->ExitRecovery (m_tcb);
              }
              else if(m_isCorruptionRecovery)
              { 
                m_tcb->m_cWndInfl = m_tcb->m_cWnd;
                m_isCorruptionRecovery = false;
              }
              NS_LOG_DEBUG ("Leaving Fast Recovery; BytesInFlight() = " <<
                            BytesInFlight () << "; cWnd = " << m_tcb->m_cWnd);
            }
          if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
            {
              m_congestionControl->IncreaseWindow (m_tcb, segsAcked);

              m_tcb->m_cWndInfl = m_tcb->m_cWnd;

              NS_LOG_LOGIC ("Congestion control called: " <<
                            " cWnd: " << m_tcb->m_cWnd <<
                            " ssTh: " << m_tcb->m_ssThresh <<
                            " segsAcked: " << segsAcked);

              NewAck (ackNumber, true);
            }
        }
    }
  // Update the pacing rate, since m_congestionControl->IncreaseWindow() or
  // m_congestionControl->PktsAcked () may change m_tcb->m_cWnd
  // Make sure that control reaches the end of this function and there is no
  // return in between
  UpdatePacingRate ();
}


void
ScpsTpSocketBase::DupAck (uint32_t currentDelivered)
{
  NS_LOG_FUNCTION (this);
  // NOTE: We do not count the DupAcks received in CA_LOSS, because we
  // don't know if they are generated by a spurious retransmission or because
  // of a real packet loss. With SACK, it is easy to know, but we do not consider
  // dupacks. Without SACK, there are some euristics in the RFC 6582, but
  // for now, we do not implement it, leading to ignoring the dupacks.
  if (m_tcb->m_congState == TcpSocketState::CA_LOSS)
    {
      return;
    }

  // RFC 6675, Section 5, 3rd paragraph:
  // If the incoming ACK is a duplicate acknowledgment per the definition
  // in Section 2 (regardless of its status as a cumulative
  // acknowledgment), and the TCP is not currently in loss recovery
  // the TCP MUST increase DupAcks by one ...
  if (m_tcb->m_congState != TcpSocketState::CA_RECOVERY)
    {
      ++m_dupAckCount;
    }

  if (m_tcb->m_congState == TcpSocketState::CA_OPEN)
    {
      // From Open we go Disorder
      NS_ASSERT_MSG (m_dupAckCount == 1, "From OPEN->DISORDER but with " <<
                     m_dupAckCount << " dup ACKs");

      m_congestionControl->CongestionStateSet (m_tcb, TcpSocketState::CA_DISORDER);
      m_tcb->m_congState = TcpSocketState::CA_DISORDER;

      NS_LOG_DEBUG ("CA_OPEN -> CA_DISORDER");
    }

  if (m_tcb->m_congState == TcpSocketState::CA_RECOVERY)
    {
      if (!m_sackEnabled)
        {
          // If we are in recovery and we receive a dupack, one segment
          // has left the network. This is equivalent to a SACK of one block.
          m_txBuffer->AddRenoSack ();
        }
      if (!m_congestionControl->HasCongControl ())
        {
          m_recoveryOps->DoRecovery (m_tcb, currentDelivered);
          NS_LOG_INFO (m_dupAckCount << " Dupack received in fast recovery mode."
                       "Increase cwnd to " << m_tcb->m_cWnd);
        }
    }
  else if (m_tcb->m_congState == TcpSocketState::CA_DISORDER)
    {
      // m_dupackCount should not exceed its threshold in CA_DISORDER state
      // when m_recoverActive has not been set. When recovery point
      // have been set after timeout, the sender could enter into CA_DISORDER
      // after receiving new ACK smaller than m_recover. After that, m_dupackCount
      // can be equal and larger than m_retxThresh and we should avoid entering
      // CA_RECOVERY and reducing sending rate again.
      NS_ASSERT((m_dupAckCount <= m_retxThresh) || m_recoverActive);

      // RFC 6675, Section 5, continuing:
      // ... and take the following steps:
      // (1) If DupAcks >= DupThresh, go to step (4).
      //     Sequence number comparison (m_highRxAckMark >= m_recover) will take
      //     effect only when m_recover has been set. Hence, we can avoid to use
      //     m_recover in the last congestion event and fail to enter
      //     CA_RECOVERY when sequence number is advanced significantly since
      //     the last congestion event, which could be common for
      //     bandwidth-greedy application in high speed and reliable network
      //     (such as datacenter network) whose sending rate is constrainted by
      //     TCP socket buffer size at receiver side.
      if ((m_dupAckCount == m_retxThresh) && ((m_highRxAckMark >= m_recover) || (!m_recoverActive)))
        {
          EnterRecovery (currentDelivered);
          NS_ASSERT (m_tcb->m_congState == TcpSocketState::CA_RECOVERY);
        }
      // (2) If DupAcks < DupThresh but IsLost (HighACK + 1) returns true
      // (indicating at least three segments have arrived above the current
      // cumulative acknowledgment point, which is taken to indicate loss)
      // go to step (4).
      else if (m_txBuffer->IsLost (m_highRxAckMark + m_tcb->m_segmentSize))
        {
          EnterRecovery (currentDelivered);
          NS_ASSERT (m_tcb->m_congState == TcpSocketState::CA_RECOVERY);
        }
      else
        {
          // (3) The TCP MAY transmit previously unsent data segments as per
          // Limited Transmit [RFC5681] ...except that the number of octets
          // which may be sent is governed by pipe and cwnd as follows:
          //
          // (3.1) Set HighRxt to HighACK.
          // Not clear in RFC. We don't do this here, since we still have
          // to retransmit the segment.

          if (!m_sackEnabled && m_limitedTx)
            {
              m_txBuffer->AddRenoSack ();

              // In limited transmit, cwnd Infl is not updated.
            }
        }
    }
}

uint32_t
ScpsTpSocketBase::Window (void) const
{ 
  if (m_lossType == ScpsTpSocketBase::Link_Outage)
    {
      return m_tcb->m_segmentSize;
    }
  else
    {
      return std::min (m_rWnd.Get (), m_tcb->m_cWnd.Get ());
    }
}

void
ScpsTpSocketBase::LinkOutPersistTimeout ()
{ 

  NS_LOG_LOGIC ("LinkOutPersistTimeout expired at " << Simulator::Now ().GetSeconds ());
  m_persistTimeout = std::min (Seconds (60), Time (2 * m_persistTimeout)); // max persist timeout = 60s
  Ptr<Packet> p = m_txBuffer->CopyFromSequence (1, m_tcb->m_nextTxSequence)->GetPacketCopy ();
  m_txBuffer->ResetLastSegmentSent ();
  TcpHeader tcpHeader;
  tcpHeader.SetSequenceNumber (m_tcb->m_nextTxSequence);
  tcpHeader.SetAckNumber (m_tcb->m_rxBuffer->NextRxSequence ());
  tcpHeader.SetWindowSize (AdvertisedWindowSize ());
  if (m_endPoint != nullptr)
    {
      tcpHeader.SetSourcePort (m_endPoint->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      tcpHeader.SetSourcePort (m_endPoint6->GetLocalPort ());
      tcpHeader.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (tcpHeader);
  //Send a packet tag for setting ECT bits in IP header
  if (m_tcb->m_ecnState != TcpSocketState::ECN_DISABLED)
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (MarkEcnCodePoint (0, m_tcb->m_ectCodePoint));
      p->AddPacketTag (ipTosTag);

      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (MarkEcnCodePoint (0, m_tcb->m_ectCodePoint));
      p->AddPacketTag (ipTclassTag);
    }
  m_txTrace (p, tcpHeader, this);

  if (m_endPoint != nullptr)
    {
      m_scpstp->SendPacket (p, tcpHeader, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_scpstp->SendPacket (p, tcpHeader, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }

  NS_LOG_LOGIC ("Schedule persist timeout at time "
                << Simulator::Now ().GetSeconds () << " to expire at time "
                << (Simulator::Now () + m_persistTimeout).GetSeconds ());
  m_linkOutPersistEvent = Simulator::Schedule (m_persistTimeout, &ScpsTpSocketBase::LinkOutPersistTimeout, this);
}

/* Received a packet upon ESTABLISHED state. This function is mimicking the
    role of tcp_rcv_established() in tcp_input.c in Linux kernel. */
void
ScpsTpSocketBase::ProcessEstablished (Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  NS_LOG_FUNCTION (this << tcpHeader);

  // Extract the flags. PSH, URG, CWR and ECE are disregarded.
  uint8_t tcpflags = tcpHeader.GetFlags () & ~(TcpHeader::PSH | TcpHeader::URG | TcpHeader::CWR | TcpHeader::ECE);

  // Different flags are different events
  if (tcpflags == TcpHeader::ACK)
    {
      if (tcpHeader.GetAckNumber () >= m_tcb->m_nextTxSequence + 1 && m_lossType == ScpsTpSocketBase::Link_Outage)
      {//退出linkoutage
        SetLossType(ScpsTpSocketBase::Corruption);
        if (m_linkOutPersistEvent.IsRunning ())
        { 
          NS_LOG_LOGIC ("LinkOutPersistTimeout canceled at " << Simulator::Now ().GetSeconds ());
          m_linkOutPersistEvent.Cancel ();
          SendPendingData (m_connected);
        }
        
        
      }
      if (tcpHeader.GetAckNumber () < m_txBuffer->HeadSequence ())
        {
          // Case 1:  If the ACK is a duplicate (SEG.ACK < SND.UNA), it can be ignored.
          // Pag. 72 RFC 793
          NS_LOG_WARN ("Ignored ack of " << tcpHeader.GetAckNumber () <<
                       " SND.UNA = " << m_txBuffer->HeadSequence ());

          // TODO: RFC 5961 5.2 [Blind Data Injection Attack].[Mitigation]
        }
      else if (tcpHeader.GetAckNumber () > m_tcb->m_highTxMark)
        {
          // If the ACK acks something not yet sent (SEG.ACK > HighTxMark) then
          // send an ACK, drop the segment, and return.
          // Pag. 72 RFC 793
          NS_LOG_WARN ("Ignored ack of " << tcpHeader.GetAckNumber () <<
                       " HighTxMark = " << m_tcb->m_highTxMark);

          // Receiver sets ECE flags when it receives a packet with CE bit on or sender hasn’t responded to ECN echo sent by receiver
          if (m_tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || m_tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
            {
              SendEmptyPacket (TcpHeader::ACK | TcpHeader::ECE);
              NS_LOG_DEBUG (TcpSocketState::EcnStateName[m_tcb->m_ecnState] << " -> ECN_SENDING_ECE");
              m_tcb->m_ecnState = TcpSocketState::ECN_SENDING_ECE;
            }
          else
            {
              SendEmptyPacket (TcpHeader::ACK);
            }
        }
      else
        {
          // SND.UNA < SEG.ACK =< HighTxMark
          // Pag. 72 RFC 793
          ReceivedAck (packet, tcpHeader);
        }
    }
  else if (tcpflags == TcpHeader::SYN)
    { // Received SYN, old NS-3 behaviour is to set state to SYN_RCVD and
      // respond with a SYN+ACK. But it is not a legal state transition as of
      // RFC793. Thus this is ignored.
    }
  else if (tcpflags == (TcpHeader::SYN | TcpHeader::ACK))
    { // No action for received SYN+ACK, it is probably a duplicated packet
    }
  else if (tcpflags == TcpHeader::FIN || tcpflags == (TcpHeader::FIN | TcpHeader::ACK))
    { // Received FIN or FIN+ACK, bring down this socket nicely
      PeerClose (packet, tcpHeader);
    }
  else if (tcpflags == 0)
    { // No flags means there is only data
      ReceivedData (packet, tcpHeader);
      if (m_tcb->m_rxBuffer->Finished ())
        {
          PeerClose (packet, tcpHeader);
        }
    }
  else
    { // Received RST or the TCP flags is invalid, in either case, terminate this socket
      if (tcpflags != TcpHeader::RST)
        { // this must be an invalid flag, send reset
          NS_LOG_LOGIC ("Illegal flag " << TcpHeader::FlagsToString (tcpflags) << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}
void
ScpsTpSocketBase::AddOptionSnack(TcpHeader &header, uint16_t hole1Offset, uint16_t hole1Size)
{
  //calculation of the option is done in the SendEmptyPacket function
  NS_LOG_FUNCTION (this);
  Ptr<ScpsTpOptionSnack> option = Create<ScpsTpOptionSnack> ();
  option->SetHole1Offset (hole1Offset);
  option->SetHole1Size (hole1Size);
  header.AddOption (option);
  
  NS_LOG_INFO (m_node->GetId () << " Add option SNACK " << *option);
}

}