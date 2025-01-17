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
#include "ns3/scpstp-option-snack.h"

namespace ns3 {

class Packet;

class ScpsTpTxBuffer : public TcpTxBuffer
{
public:
  static TypeId GetTypeId (void);

  ScpsTpTxBuffer (uint32_t n = 0);

  ScpsTpTxBuffer (const TcpTxBuffer &buffer);


  virtual ~ScpsTpTxBuffer (void);

  virtual void UpdateSnackedData (const ScpsTpOptionSnack::SnackList &snackList);

  /**
   * \brief Get a block (which is returned as Packet) from a list
   *
   * This function extract a block [requestedSeq,numBytes) from the list, which
   * starts at startingSeq.
   *
   * The cases we need to manage are two, and they are depicted in the following
   * image:
   *
   *\verbatim
                       |------|     |----|     |----|
                list = |      | --> |    | --> |    |
                       |------|     |----|     |----|

                       ^      ^
                       | ^ ^  |         (1)
                     seq | |  seq + numBytes
                         | |
                         | |
                      seq   seq + numBytes     (2)
   \endverbatim
   *
   * The case 1 is easy to manage: the requested block is exactly a packet
   * already stored. If one value (seq or seq + numBytes) does not align
   * to a packet boundary, or when both values does not align (case 2), it is
   * a bit more complex.
   *
   * Basically, we have two possible operations:
   *
   *  - fragment : split an existing packet in two
   *  - merge    : merge two existing packets in one
   *
   * and we reduce case (2) to case (1) through sequentially applying fragment
   * or merge. For instance:
   *
   *\verbatim
      |------|
      |      |
      |------|

      ^ ^  ^ ^
      | |  | |
  start |  | |
        |  | end
       seq |
           seq + numBytes
   \endverbatim
   *
   * To reduce to case (1), we need to perform two fragment operations:
   *
   * - fragment (start, seq)
   * - fragment (seq + numBytes, end)
   *
   * After these operations, the requested block is exactly the resulting packet.
   * Merge operation is required when the requested block span over two (or more)
   * existing packets.
   *
   * While this could be extremely slow in the worst possible scenario (one big
   * packet which is split in small packets for transmission, and merged for
   * re-transmission) that scenario is unlikely during a TCP transmission (since
   * MSS can change, but it is stable, and retransmissions do not happen for
   * each segment).
   *
   * \param list List to extract block from
   * \param startingSeq Starting sequence of the list
   * \param numBytes Bytes to extract, starting from requestedSeq
   * \param requestedSeq Requested sequence
   * \param listEdited output parameter which indicates if the list has been edited
   * \return the item that contains the right packet
   */
  virtual TcpTxItem* GetPacketFromList (PacketList &list, const SequenceNumber32 &startingSeq,
                                uint32_t numBytes, const SequenceNumber32 &requestedSeq,
                                bool *listEdited = nullptr) const;

  /**
   * \brief Merge two TcpTxItem
   *
   * Merge t2 in t1. It consists in copying the lastSent field if t2 is more
   * recent than t1. Retransmitted field is copied only if it set in t2 but not
   * in t1. Sacked is copied only if it is true in both items.
   *
   * \param t1 first item
   * \param t2 second item
   */
  virtual void MergeItems (TcpTxItem *t1, TcpTxItem *t2) const;

  void MarkLostPacketsInRange (PacketList &list, const SequenceNumber32 &listStartFrom, const SequenceNumber32 &startSeq, 
                                         const SequenceNumber32 &endSeq, bool *listEdited) const;

private:


};




} // namespace ns3



#endif /* SCPSTP_TX_BUFFER_H */