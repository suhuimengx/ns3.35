/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
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

#include "ns3/packet.h"
#include "ns3/log.h"
#include "scpstp-rx-buffer.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ScpsTpRxBuffer");

NS_OBJECT_ENSURE_REGISTERED (ScpsTpRxBuffer);

TypeId
ScpsTpRxBuffer::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ScpsTpRxBuffer")
    .SetParent<TcpRxBuffer> ()
    .SetGroupName ("Internet")
    .AddConstructor<ScpsTpRxBuffer> ()
  ;
  return tid;
}

ScpsTpRxBuffer::ScpsTpRxBuffer (uint32_t n)
  : TcpRxBuffer (n)
{
}

ScpsTpRxBuffer::~ScpsTpRxBuffer ()
{
}





} // namespace ns3