/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010-2015 Adrian Sai-wah Tam
 * Copyright (c) 2016 Natale Patriciello <natale.patriciello@gmail.com>
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
 * Original author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */

#include <algorithm>
#include <iostream>

#include "ns3/packet.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-option-ts.h"

#include"scpstp-tx-buffer.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ScpsTpTxBuffer");
NS_OBJECT_ENSURE_REGISTERED (ScpsTpTxBuffer);

TypeId
ScpsTpTxBuffer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ScpsTpTxBuffer")
    .SetParent<TcpTxBuffer> ()
    .SetGroupName ("Internet")
    .AddConstructor<ScpsTpTxBuffer> ()
  ;
  return tid;
}

ScpsTpTxBuffer::ScpsTpTxBuffer (uint32_t n)
  : TcpTxBuffer (n)
{
}

ScpsTpTxBuffer::ScpsTpTxBuffer (const TcpTxBuffer &buffer)
  : TcpTxBuffer (buffer)
{
}
ScpsTpTxBuffer::~ScpsTpTxBuffer (void)
{
}

SequenceNumber32
ScpsTpTxBuffer::HeadSequence (void) const
{
  NS_LOG_FUNCTION (this);
  return m_firstByteSeq;
}


} // namespace ns3