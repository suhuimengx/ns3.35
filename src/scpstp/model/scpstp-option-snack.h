/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Adrian Sai-wah Tam
 * Copyright (c) 2015 ResiliNets, ITTC, University of Kansas
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
 * Original Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 * Documentation, test cases: Truc Anh N. Nguyen   <annguyen@ittc.ku.edu>
 *                            ResiliNets Research Group   http://wiki.ittc.ku.edu/resilinets
 *                            The University of Kansas
 *                            James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 */

#ifndef SCPSTP_OPTION_SNACK_H
#define SCPSTP_OPTION_SNACK_H

#include "ns3/tcp-option.h"
#include "ns3/sequence-number.h"

namespace ns3 {
/**
 * \brief Defines the SCPSTP option of SNACK
 */
class ScpsTpOptionSnack : public TcpOption
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;

  typedef std::pair<SequenceNumber32, SequenceNumber32> SnackHole; //!< SNACK hole definition
  typedef std::list<SnackHole> SnackList;                           //!< SNACK list definition

  ScpsTpOptionSnack ();
  virtual ~ScpsTpOptionSnack ();

  virtual void Print (std::ostream &os) const;
  /**
   * \brief Serialize the option
   * \param start the iterator to the start of the buffer
   * \param acknumber the ack number
   */
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

  virtual uint8_t GetKind (void) const;
  virtual uint32_t GetSerializedSize (void) const;

  /**
   * \brief Add a SNACK hole
   * \param s the SNACK hole to be added
   */
  void AddSnackHole (SnackHole s);

  /**
   * \brief Count the total number of SNACK holess
   * \return the total number of SNACK holess
   */
  uint32_t GetNumSnackHoles (void) const;

  /**
   * \brief Clear the SNACK list
   */
  void ClearSnackList (void);

  /**
   * \brief Get the SNACK list
   * \return the SNACK list
   */
  SnackList GetSnackList (void) const;

  /**
   * \brief Get the offset of the first hole
   * \return the offset of the first hole
   */
  uint16_t GetHole1Offset (void) const;

  /**
   * \brief Get the size of the first hole
   * \return the size of the first hole
   */
  uint16_t GetHole1Size (void) const;

  void SetHole1Offset (uint16_t offset);

  void SetHole1Size (uint16_t size);


  friend std::ostream & operator<< (std::ostream & os, ScpsTpOptionSnack const & snackOption);

protected:
  uint16_t m_hole1Offset; //!< the offset of the first hole
  uint16_t m_hole1Size; //!< the size of the first hole
  SnackList m_snackList; //!< the list of SNACK holes
};

/**
 * \brief Output operator.
 * \param os The output stream.
 * \param snackOption the option to print.
 * \returns The output stream.
 */
std::ostream & operator<< (std::ostream & os,
                           ScpsTpOptionSnack const & snackOption);

/**
 * \brief Output operator.
 * \param os The output stream.
 * \param snackHole the hole to print.
 * \returns The output stream.
 */
std::ostream & operator<< (std::ostream & os,
                           ScpsTpOptionSnack::SnackHole const & snackHole);
  
    

} // namespace ns3
#endif /* SCPSTP_OPTION_SNACK */