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

#ifndef SCPSTP_TX_BUFFER_H
#define SCPSTP_TX_BUFFER_H


#include "ns3/object.h"
#include "ns3/traced-value.h"
#include "ns3/sequence-number.h"
#include "ns3/tcp-option-sack.h"
#include "ns3/tcp-tx-item.h"
#include "ns3/tcp-tx-buffer.h"

namespace ns3 {

class Packet;

class ScpsTpTxBuffer : public TcpTxBuffer
{
public:
  static TypeId GetTypeId (void);

  ScpsTpTxBuffer (uint32_t n = 0);
  virtual ~ScpsTpTxBuffer (void);

private:


};




} // namespace ns3



#endif /* SCPSTP_TX_BUFFER_H */