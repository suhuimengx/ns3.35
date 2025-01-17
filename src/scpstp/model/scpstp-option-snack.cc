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

#include "scpstp-option-snack.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ScpsTpOptionSnack");

NS_OBJECT_ENSURE_REGISTERED (ScpsTpOptionSnack);

ScpsTpOptionSnack::ScpsTpOptionSnack ()
  : TcpOption ()
{
}

ScpsTpOptionSnack::~ScpsTpOptionSnack ()
{
}

TypeId
ScpsTpOptionSnack::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::ScpsTpOptionSnack")
    .SetParent<TcpOption> ()
    .SetGroupName ("Internet")
    .AddConstructor<ScpsTpOptionSnack> ()
  ;
  return tid;
}


TypeId
ScpsTpOptionSnack::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void
ScpsTpOptionSnack::Print (std::ostream &os) const
{
 os << "hole1offset: " << m_hole1Offset << ", hole1size: " << m_hole1Size;
}

uint32_t
ScpsTpOptionSnack::GetSerializedSize (void) const
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Serialized size: " << 6);
  return 6;
}

void
ScpsTpOptionSnack::Serialize (Buffer::Iterator start) const
{
  NS_LOG_FUNCTION (this);
  NS_LOG_INFO("Serializing SNACK option:" << m_hole1Offset << " " << m_hole1Size);
  Buffer::Iterator i = start;
  i.WriteU8 (GetKind ()); // Kind in the first byte
  uint8_t length =  6; // 2  for the kind and length, 2  for hole1 offset, 2 for hole1 size
  i.WriteU8 (length); // Length
  i.WriteHtonU16 (m_hole1Offset); // Hole1 offset
  i.WriteHtonU16 (m_hole1Size);   // Hole1 size

}

uint32_t
ScpsTpOptionSnack::Deserialize (Buffer::Iterator start)
{
  NS_LOG_FUNCTION (this);
  Buffer::Iterator i = start;
  uint8_t readKind = i.ReadU8 ();
  if (readKind != GetKind ())
    {
      NS_LOG_WARN ("Malformed SNACK option, wrong type");
      return 0;
    }

  uint8_t size = i.ReadU8 ();
  NS_LOG_LOGIC ("Size: " << static_cast<uint32_t> (size));
  m_hole1Offset = i.ReadNtohU16 ();
  m_hole1Size = i.ReadNtohU16 ();
  return GetSerializedSize ();
}

uint8_t
ScpsTpOptionSnack::GetKind (void) const
{
  return TcpOption::SNACK;
}

void
ScpsTpOptionSnack::AddSnackHole (SnackHole s)
{
  NS_LOG_FUNCTION (this);
  m_snackList.push_back (s);
}

uint32_t
ScpsTpOptionSnack::GetNumSnackHoles (void) const
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Number of SNACK holes appended: " << m_snackList.size ());
  return static_cast<uint32_t> (m_snackList.size ());
}

void
ScpsTpOptionSnack::ClearSnackList (void)
{
  m_snackList.clear ();
}

ScpsTpOptionSnack::SnackList
ScpsTpOptionSnack::GetSnackList (void) const
{
  NS_LOG_FUNCTION (this);
  return m_snackList;
}

uint16_t
ScpsTpOptionSnack::GetHole1Offset (void) const
{
  NS_LOG_FUNCTION (this);
  return m_hole1Offset;
}

uint16_t
ScpsTpOptionSnack::GetHole1Size (void) const
{
  NS_LOG_FUNCTION (this);
  return m_hole1Size;
}

void 
ScpsTpOptionSnack::SetHole1Offset (uint16_t offset)
{
  NS_LOG_FUNCTION (this);
  m_hole1Offset = offset;
}

void
ScpsTpOptionSnack::SetHole1Size (uint16_t size)
{
  NS_LOG_FUNCTION (this);
  m_hole1Size = size;
}

std::ostream &
operator<< (std::ostream & os, ScpsTpOptionSnack const & snackOption)
{
  std::stringstream ss;
  ss << "{";
  for (auto it = snackOption.m_snackList.begin (); it != snackOption.m_snackList.end (); ++it)
    {
      ss << *it;
    }
  ss << "}";
  os << ss.str ();
  return os;
}

std::ostream &
operator<< (std::ostream & os, ScpsTpOptionSnack::SnackHole const & snackHole)
{
  std::stringstream ss;
  ss << "[" << snackHole.first << ";" << snackHole.second << "]";
  os << ss.str ();
  return os;
}

} // namespace ns3
