/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 ResiliNets, ITTC, University of Kansas
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
 * Author: Truc Anh N. Nguyen <annguyen@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 */

#include "tcp-timely.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpTimely");
NS_OBJECT_ENSURE_REGISTERED (TcpTimely);

TypeId
TcpTimely::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpTimely")
    .SetParent<TcpNewReno> ()
    .AddConstructor<TcpTimely> ()
    .SetGroupName ("Internet")
    .AddAttribute("EMWA", "Exponential Moving Weight parameter",
                   DoubleValue(0.1),
                   MakeDoubleAccessor (&TcpTimely::m_alpha),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Addstep", "Additive increase",
                   DoubleValue(1),
                   MakeDoubleAccessor (&TcpTimely::m_delta),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("Beta", "Multiplicative decrease",
                   DoubleValue (0.8),
                   MakeDoubleAccessor (&TcpTimely::m_beta),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("THigh", "Limit on increase",
                   DoubleValue (50000),
                   MakeDoubleAccessor (&TcpTimely::m_thigh),
                   MakeDoubleChecker<double> ())
    .AddAttribute ("TLow", "Filter on RTT spikes",
                   DoubleValue (2000),
                   MakeDoubleAccessor (&TcpTimely::m_tlow),
                   MakeDoubleChecker<double> ())
  ;
  return tid;
}

TcpTimely::TcpTimely (void)
  : TcpNewReno (),
    m_alpha (0.1),
    m_beta (0.8),
    m_delta (1),
    m_baseRtt (Time::Max ()),
    m_minRtt (TMP_MAX),
    m_cntRtt (0),
    m_doingTimelyNow (true),
    m_begSndNxt (0),
    m_thigh(50000),
    m_tlow(2000),
    m_prevRtt(Time::Max()),
    m_rttDiff(0)
{
  NS_LOG_FUNCTION (this);
}

TcpTimely::TcpTimely (const TcpTimely& sock)
  : TcpNewReno (sock),
    m_alpha (sock.m_alpha),
    m_beta (sock.m_beta),
    m_delta (sock.m_delta),
    m_baseRtt (sock.m_baseRtt),
    m_minRtt (sock.m_minRtt),
    m_cntRtt (sock.m_cntRtt),
    m_doingTimelyNow (true),
    m_begSndNxt (0),
    m_thigh(sock.m_thigh),
    m_tlow(sock.m_tlow),
    m_prevRtt(sock.m_prevRtt),
    m_rttDiff(0)
    
{
  NS_LOG_FUNCTION (this);
}

TcpTimely::~TcpTimely (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps>
TcpTimely::Fork (void)
{
  return CopyObject<TcpTimely> (this);
}

void
TcpTimely::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                     const Time& rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);

  if (rtt.IsZero ())
    {
      return;
    }

  new_rtt=rtt.GetMicroSeconds();
  //NS_LOG_DEBUG("new_rtt = " << new_rtt);
  //m_minRtt = std::min (m_minRtt, rtt);
  //NS_LOG_DEBUG ("Updated m_minRtt = " << m_minRtt);

  m_baseRtt = std::min (m_baseRtt, rtt);
  //NS_LOG_DEBUG ("Updated m_baseRtt = " << m_baseRtt);

  // Update RTT counter
  m_cntRtt++;
  //NS_LOG_DEBUG ("Updated m_cntRtt = " << m_cntRtt);
}

void
TcpTimely::EnableTimely(Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);

  m_doingTimelyNow = true;
  m_begSndNxt = tcb->m_nextTxSequence;
  m_cntRtt = 0;
  m_minRtt = Time(2000000);
}

void
TcpTimely::DisableTimely ()
{
  NS_LOG_FUNCTION (this);

  m_doingTimelyNow = false;
}

void
TcpTimely::CongestionStateSet (Ptr<TcpSocketState> tcb,
                              const TcpSocketState::TcpCongState_t newState)
{
  NS_LOG_FUNCTION (this << tcb << newState);
  if (newState == TcpSocketState::CA_OPEN)
    {
      EnableTimely (tcb);
    }
  else
    {
      DisableTimely ();
    }
}

void
TcpTimely::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  if (!m_doingTimelyNow)
    {
      // If Vegas is not on, we follow NewReno algorithm
      NS_LOG_LOGIC ("Timely is not turned on, we follow NewReno algorithm.");
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
      return;
    }

    if (tcb->m_lastAckedSeq >= m_begSndNxt)
    { // A Vegas cycle has finished, we do Vegas cwnd adjustment every RTT.

      NS_LOG_LOGIC ("A Timely cycle has finished, we adjust cwnd once per RTT.");

      // Save the current right edge for next Vegas cycle
      m_begSndNxt = tcb->m_nextTxSequence;
      
      double new_rtt_diff_us = new_rtt- m_prevRtt.GetMicroSeconds();
      m_prevRtt = Time(new_rtt);
      m_rttDiff = (1 - m_alpha ) * m_rttDiff + m_alpha * new_rtt_diff_us;
      double normalized_gradient = m_rttDiff / (m_minRtt.GetMicroSeconds()); 

      if(new_rtt<m_tlow)
      {
        NS_LOG_INFO("Too low");
        m_compEvents = 0;
        m_rate=m_rate+m_delta;
        NS_LOG_INFO("m_rate: " << m_rate);
        NS_LOG_INFO("new_rtt: " << new_rtt);
        NS_LOG_INFO("m_minRtt: " << m_minRtt.GetMicroSeconds());

        tcb->m_cWnd=m_rate*(m_minRtt.GetMicroSeconds());
        NS_LOG_INFO("window size is now: " << tcb->m_cWnd);
        return;
      }
      else if(new_rtt > m_thigh){
        NS_LOG_INFO( "too high" );
        m_compEvents = 0;
        m_rate = m_rate * (1 - m_beta * (1 - m_thigh/new_rtt));
        tcb->m_cWnd = m_rate * (m_minRtt.GetMicroSeconds());
        NS_LOG_INFO("window size is now: " << tcb->m_cWnd);
        return;
      }
      else if(normalized_gradient <=0){
        NS_LOG_INFO( "normalized gradient" );
        m_compEvents += 1;
        int N = 1;
        if (m_compEvents >= 5) {
                NS_LOG_INFO( "Entering HAI mode" );
                N = 5;
                m_compEvents = 0; 
        }
        m_rate = m_rate + N * m_delta; 
      }
      else{
        m_rate = m_rate * (1 - m_beta* normalized_gradient);
        m_compEvents = 0;
       }
        tcb->m_cWnd = m_rate * (m_minRtt.GetMicroSeconds());
        NS_LOG_INFO("window size is now: " << tcb->m_cWnd);


        m_baseRtt = std::min (m_baseRtt, Time(new_rtt));
        //NS_LOG_INFO ("Updated m_baseRtt = " << m_baseRtt);

         //m_minRtt = (Time::Max());

    }
}

std::string
TcpTimely::GetName () const
{
  return "TcpTimely";
}

uint32_t
TcpTimely::GetSsThresh (Ptr<const TcpSocketState> tcb,
                       uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return std::max (std::min (tcb->m_ssThresh.Get (), tcb->m_cWnd.Get () - tcb->m_segmentSize), 2 * tcb->m_segmentSize);
}

} // namespace ns3
