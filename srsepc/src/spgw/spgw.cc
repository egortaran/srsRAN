/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2017 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <iostream> 
#include <algorithm>
#include <boost/thread/mutex.hpp>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include "spgw/spgw.h"
#include "mme/mme_gtpc.h"


namespace srsepc{

spgw*          spgw::m_instance = NULL;
boost::mutex  spgw_instance_mutex;

const uint16_t SPGW_BUFFER_SIZE = 2500;

spgw::spgw():
  m_running(false),
  m_sgi_up(false),
  m_s1u_up(false),
  m_next_ctrl_teid(1),
  m_next_user_teid(1)
{
  return;
}

spgw::~spgw()
{
  return;
}

spgw*
spgw::get_instance(void)
{
  boost::mutex::scoped_lock lock(spgw_instance_mutex);
  if(NULL == m_instance) {
    m_instance = new spgw();
  }
  return(m_instance);
}

void
spgw::cleanup(void)
{
  boost::mutex::scoped_lock lock(spgw_instance_mutex);
  if(NULL != m_instance) {
    delete m_instance;
    m_instance = NULL;
  }
}

int
spgw::init(spgw_args_t* args, srslte::log_filter *spgw_log)
{
  srslte::error_t err;
  m_pool = srslte::byte_buffer_pool::get_instance();

  //Init log
  m_spgw_log = spgw_log;
  m_mme_gtpc = mme_gtpc::get_instance();

  //Init SGi interface
  err = init_sgi_if(args);
  if (err != srslte::ERROR_NONE)
  {
    m_spgw_log->console("Could not initialize the SGi interface.\n");
    return -1;
  }

  //Init S1-U
  err = init_s1u(args);
  if (err != srslte::ERROR_NONE)
  {
    m_spgw_log->console("Could not initialize the S1-U interface.\n");
    return -1;
  }
  m_spgw_log->info("SP-GW Initialized.\n");
  m_spgw_log->console("SP-GW Initialized.\n");
  return 0;
}

void
spgw::stop()
{
  if(m_running)
  {
    m_running = false;
    thread_cancel();
    wait_thread_finish();

    //Clean up SGi interface
    if(m_sgi_up)
    {
      close(m_sgi_if);
      close(m_sgi_sock);
    }
    //Clean up S1-U socket
    if(m_s1u_up)
    {
      close(m_s1u);
    }
  }
  return;
}

srslte::error_t
spgw::init_sgi_if(spgw_args_t *args)
{
  char dev[IFNAMSIZ] = "srs_spgw_sgi";
  struct ifreq ifr;

  if(m_sgi_up)
  {
    return(srslte::ERROR_ALREADY_STARTED);
  }


  // Construct the TUN device
  m_sgi_if = open("/dev/net/tun", O_RDWR);
  m_spgw_log->info("TUN file descriptor = %d\n", m_sgi_if);
  if(m_sgi_if < 0)
  {
      m_spgw_log->error("Failed to open TUN device: %s\n", strerror(errno));
      return(srslte::ERROR_CANT_START);
  }

  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  strncpy(ifr.ifr_ifrn.ifrn_name, dev, IFNAMSIZ);
  if(ioctl(m_sgi_if, TUNSETIFF, &ifr) < 0)
  {
      m_spgw_log->error("Failed to set TUN device name: %s\n", strerror(errno));
      close(m_sgi_if);
      return(srslte::ERROR_CANT_START);
  }

  // Bring up the interface
  m_sgi_sock = socket(AF_INET, SOCK_DGRAM, 0);

  if(ioctl(m_sgi_sock, SIOCGIFFLAGS, &ifr) < 0)
  {
      m_spgw_log->error("Failed to bring up socket: %s\n", strerror(errno));
      close(m_sgi_if);
      return(srslte::ERROR_CANT_START);
  }
  ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
  if(ioctl(m_sgi_sock, SIOCSIFFLAGS, &ifr) < 0)
  {
      m_spgw_log->error("Failed to set socket flags: %s\n", strerror(errno));
      close(m_sgi_if);
      return(srslte::ERROR_CANT_START);
  }

  //Set IP of the interface
  struct sockaddr_in *addr = (struct sockaddr_in*)&ifr.ifr_addr;
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = inet_addr(args->sgi_if_addr.c_str());
  addr->sin_port = 0;

  if (ioctl(m_sgi_sock, SIOCSIFADDR, &ifr) < 0) {
    m_spgw_log->error("Failed to set TUN interface IP. Address: %s, Error: %s\n", args->sgi_if_addr.c_str(), strerror(errno));
    close(m_sgi_if);
    close(m_sgi_sock);
    return srslte::ERROR_CANT_START;
  }

  ifr.ifr_netmask.sa_family                                 = AF_INET;
  ((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr.s_addr = inet_addr("255.255.255.0");
  if (ioctl(m_sgi_sock, SIOCSIFNETMASK, &ifr) < 0) {
    m_spgw_log->error("Failed to set TUN interface Netmask. Error: %s\n", strerror(errno));
    close(m_sgi_if);
    close(m_sgi_sock);
    return srslte::ERROR_CANT_START;
  }

  m_sgi_up = true;
  return(srslte::ERROR_NONE);
}

srslte::error_t
spgw::init_s1u(spgw_args_t *args)
{
  //Open S1-U socket
  m_s1u = socket(AF_INET,SOCK_DGRAM,0);
  if (m_s1u == -1)
  {
    m_spgw_log->error("Failed to open socket: %s\n", strerror(errno));
    return srslte::ERROR_CANT_START;
  }
  m_s1u_up = true;

  //Bind the socket
  m_s1u_addr.sin_family = AF_INET;
  m_s1u_addr.sin_addr.s_addr=inet_addr(args->gtpu_bind_addr.c_str());
  m_s1u_addr.sin_port=htons(GTPU_RX_PORT);

  if (bind(m_s1u,(struct sockaddr *)&m_s1u_addr,sizeof(struct sockaddr_in))) {
    m_spgw_log->error("Failed to bind socket: %s\n", strerror(errno));
    return srslte::ERROR_CANT_START;
  }
  m_spgw_log->info("S1-U socket = %d\n", m_s1u);
  m_spgw_log->info("S1-U IP = %s, Port = %d \n", inet_ntoa(m_s1u_addr.sin_addr),ntohs(m_s1u_addr.sin_port));

  return srslte::ERROR_NONE;
}

void
spgw::run_thread()
{
  //Mark the thread as running
  m_running=true;
  srslte::byte_buffer_t *msg;
  msg = m_pool->allocate();

  struct sockaddr src_addr;
  socklen_t addrlen;

  int sgi = m_sgi_if;

  fd_set set;
  //struct timeval to;
  int max_fd = std::max(m_s1u,sgi);
  while (m_running)
  {
    msg->reset();
    FD_ZERO(&set);
    FD_SET(m_s1u, &set);
    FD_SET(sgi, &set);

    //m_spgw_log->info("Waiting for S1-U or SGi packets.\n");
    int n = select(max_fd+1, &set, NULL, NULL, NULL);
    if (n == -1)
    {
      m_spgw_log->error("Error from select\n");
    }
    else if (n)
    {
      //m_spgw_log->info("Data is available now.\n");
      if (FD_ISSET(m_s1u, &set))
      {
          msg->N_bytes = recvfrom(m_s1u, msg->msg, SRSLTE_MAX_BUFFER_SIZE_BYTES, 0, &src_addr, &addrlen );
          //m_spgw_log->console("Received PDU from S1-U. Bytes %d\n", msg->N_bytes);
          //m_spgw_log->debug("Received PDU from S1-U. Bytes %d\n", msg->N_bytes);
      }
      if (FD_ISSET(m_sgi_if, &set))
      {
          //m_spgw_log->console("Received PDU from SGi\n");
          msg->N_bytes = read(sgi, msg->msg, SRSLTE_MAX_BUFFER_SIZE_BYTES);
          //m_spgw_log->console("Received PDU from SGi. Bytes %d\n", msg->N_bytes);
          //m_spgw_log->debug("Received PDU from SGi. Bytes %d\n", msg->N_bytes);
      }
    }
    else
    {
      m_spgw_log->debug("No data from select.\n");
    }
  }
  m_pool->deallocate(msg);
  return;
}


uint64_t
spgw::get_new_ctrl_teid()
{
  return m_next_ctrl_teid++;
}

uint64_t
spgw::get_new_user_teid()
{
  return m_next_user_teid++;
}

in_addr_t
spgw::get_new_ue_ipv4()
{
  return inet_addr("172.0.0.2");//FIXME Tmp hack
}

void
spgw::handle_create_session_request(struct srslte::gtpc_create_session_request *cs_req, struct srslte::gtpc_pdu *cs_resp_pdu)
{
  srslte::gtpc_header *header = &cs_resp_pdu->header;
  srslte::gtpc_create_session_response *cs_resp = &cs_resp_pdu->choice.create_session_response;


  m_spgw_log->info("Received Create Session Request\n");
    //Setup uplink control TEID
  uint64_t spgw_uplink_ctrl_teid = get_new_ctrl_teid();
  //Setup uplink user TEID
  uint64_t spgw_uplink_user_teid = get_new_user_teid();
  //Allocate UE IP
  in_addr_t ue_ip = get_new_ue_ipv4();

  //Save the UE IP to User TEID map //TODO!!!
  spgw_tunnel_ctx_t *tunnel_ctx = new spgw_tunnel_ctx_t;
  tunnel_ctx->imsi = cs_req->imsi;
  tunnel_ctx->up_user_fteid.teid = spgw_uplink_user_teid;
  tunnel_ctx->up_user_fteid.ipv4 = m_s1u_addr.sin_addr.s_addr;
  tunnel_ctx->dw_ctrl_fteid.teid = cs_req->sender_f_teid.teid;
  tunnel_ctx->dw_ctrl_fteid.ipv4 = cs_req->sender_f_teid.ipv4;

  tunnel_ctx->up_ctrl_fteid.teid = spgw_uplink_ctrl_teid;
  tunnel_ctx->ue_ipv4 = ue_ip;
  m_teid_to_tunnel_ctx.insert(std::pair<uint32_t,spgw_tunnel_ctx_t*>(spgw_uplink_ctrl_teid,tunnel_ctx));
  //Create session response message
  //Setup GTP-C header
  header->piggyback = false;
  header->teid_present = true;
  header->teid = cs_req->sender_f_teid.teid;  //Send create session requesponse to the CS Request TEID
  header->type = srslte::GTPC_MSG_TYPE_CREATE_SESSION_RESPONSE;
  //Initialize to zero
  bzero(cs_resp,sizeof(struct srslte::gtpc_create_session_response));
  //Setup Cause
  cs_resp->cause.cause_value = srslte::GTPC_CAUSE_VALUE_REQUEST_ACCEPTED;
  //Setup sender F-TEID (ctrl)
  cs_resp->sender_f_teid.ipv4_present = true;
  cs_resp->sender_f_teid.teid = spgw_uplink_ctrl_teid;
  cs_resp->sender_f_teid.ipv4 = 0;//FIXME This is not relevant, as the GTP-C is not transmitted over sockets yet.
  //Bearer context created
  cs_resp->eps_bearer_context_created.ebi = 5;
  cs_resp->eps_bearer_context_created.cause.cause_value = srslte::GTPC_CAUSE_VALUE_REQUEST_ACCEPTED;
  cs_resp->eps_bearer_context_created.s1_u_sgw_f_teid_present=true;
  cs_resp->eps_bearer_context_created.s1_u_sgw_f_teid.teid = spgw_uplink_user_teid;
  cs_resp->eps_bearer_context_created.s1_u_sgw_f_teid.ipv4 = m_s1u_addr.sin_addr.s_addr;
  //Fill in the PAA
  cs_resp->paa_present = true;
  cs_resp->paa.pdn_type = srslte::GTPC_PDN_TYPE_IPV4;
  cs_resp->paa.ipv4_present = true;
  cs_resp->paa.ipv4 = ue_ip;
  m_spgw_log->info("Sending Create Session Response\n");
  m_mme_gtpc->handle_create_session_response(cs_resp_pdu);
  return;
}

void
spgw::handle_modify_bearer_request(struct srslte::gtpc_pdu *mb_req_pdu, struct srslte::gtpc_pdu *mb_resp_pdu)
{
  m_spgw_log->info("Received Modified Bearer Request\n");

  //Get control tunnel info from mb_req PDU
  uint32_t ctrl_teid = mb_req_pdu->header.teid;
  std::map<uint32_t,spgw_tunnel_ctx_t*>::iterator tunnel_it = m_teid_to_tunnel_ctx.find(ctrl_teid);
  if(tunnel_it == m_teid_to_tunnel_ctx.end())
  {
    m_spgw_log->warning("Could not find TEID %d to modify\n",ctrl_teid);
    return;
  }
  spgw_tunnel_ctx_t *tunnel_ctx = tunnel_it->second;

  //Store user DW link TEID
  srslte::gtpc_modify_bearer_request *mb_req = &mb_req_pdu->choice.modify_bearer_request;
  tunnel_ctx->dw_user_fteid.teid = mb_req->eps_bearer_context_to_modify.s1_u_enb_f_teid.teid;
  tunnel_ctx->dw_user_fteid.ipv4 = mb_req->eps_bearer_context_to_modify.s1_u_enb_f_teid.ipv4;
  //Set up actual tunnel
  m_spgw_log->info("Setting Up GTP-U tunnel. Tunnel info: \n");
  struct in_addr addr;
  addr.s_addr = tunnel_ctx->ue_ipv4;
  m_spgw_log->info("IMSI: %lu, UE IP, %s \n",tunnel_ctx->imsi, inet_ntoa(addr));
  m_spgw_log->info("S-GW Rx Ctrl TEID 0x%x, MME Rx Ctrl TEID 0x%x\n", tunnel_ctx->up_ctrl_fteid.teid, tunnel_ctx->dw_ctrl_fteid.teid);
  m_spgw_log->info("S-GW Rx Ctrl IP (NA), MME Rx Ctrl IP (NA)\n");
  
  struct in_addr addr2;
  addr2.s_addr = tunnel_ctx->up_user_fteid.ipv4;
  m_spgw_log->info("S-GW Rx User TEID 0x%x, S-GW Rx User IP %s\n", tunnel_ctx->up_user_fteid.teid, inet_ntoa(addr2));

  struct in_addr addr3;
  addr3.s_addr = tunnel_ctx->dw_user_fteid.ipv4;
  m_spgw_log->info("eNB Rx User TEID 0x%x, eNB Rx User IP %s\n", tunnel_ctx->dw_user_fteid.teid, inet_ntoa(addr3));
  //TODO!!!

  //Setting up Modify bearer response PDU
  //Header
  srslte::gtpc_header *header = &mb_req_pdu->header;
  header->piggyback = false;
  header->teid_present = true;
  header->teid = tunnel_ctx->dw_ctrl_fteid.teid;  //
  header->type = srslte::GTPC_MSG_TYPE_MODIFY_BEARER_RESPONSE;

  //PDU
  srslte::gtpc_modify_bearer_response *mb_resp = &mb_req_pdu->choice.modify_bearer_response;
}
} //namespace srsepc
