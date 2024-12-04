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
    m_lossType (Corruption)
{
  NS_LOG_FUNCTION (this);
  m_txBuffer = CreateObject<TcpTxBuffer> ();
  m_txBuffer->SetRWndCallback (MakeCallback (&ScpsTpSocketBase::GetRWnd, this));
  m_tcb      = CreateObject<TcpSocketState> ();
  m_rateOps  = CreateObject <TcpRateLinux> ();

  m_tcb->m_rxBuffer = CreateObject<TcpRxBuffer> ();

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
    m_scpstp (sock.m_scpstp)
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
  m_txBuffer = CopyObject (sock.m_txBuffer);
  m_txBuffer->SetRWndCallback (MakeCallback (&ScpsTpSocketBase::GetRWnd, this));
  m_tcb = CopyObject (sock.m_tcb);
  m_tcb->m_rxBuffer = CopyObject (sock.m_tcb->m_rxBuffer);

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
}