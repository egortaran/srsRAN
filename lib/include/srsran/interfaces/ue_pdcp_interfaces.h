/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#ifndef SRSRAN_UE_PDCP_INTERFACES_H
#define SRSRAN_UE_PDCP_INTERFACES_H

#include "pdcp_interface_types.h"
#include "srsran/common/byte_buffer.h"

namespace srsue {

class pdcp_interface_rrc
{
public:
  virtual void reestablish()                                                                                        = 0;
  virtual void reestablish(uint32_t lcid)                                                                           = 0;
  virtual void reset()                                                                                              = 0;
  virtual void write_sdu(uint32_t lcid, srsran::unique_byte_buffer_t sdu, int sn = -1)                              = 0;
  virtual void add_bearer(uint32_t lcid, srsran::pdcp_config_t cnfg)                                                = 0;
  virtual void del_bearer(uint32_t lcid)                                                                            = 0;
  virtual void change_lcid(uint32_t old_lcid, uint32_t new_lcid)                                                    = 0;
  virtual void config_security(uint32_t lcid, srsran::as_security_config_t sec_cfg)                                 = 0;
  virtual void config_security_all(srsran::as_security_config_t sec_cfg)                                            = 0;
  virtual void enable_integrity(uint32_t lcid, srsran::srsran_direction_t direction)                                = 0;
  virtual void enable_encryption(uint32_t                   lcid,
                                 srsran::srsran_direction_t direction = srsran::srsran_direction_t::DIRECTION_TXRX) = 0;
  virtual void send_status_report()                                                                                 = 0;
  virtual void send_status_report(uint32_t lcid)                                                                    = 0;
};

class pdcp_interface_rlc
{
public:
  /* RLC calls PDCP to push a PDCP PDU. */
  virtual void write_pdu(uint32_t lcid, srsran::unique_byte_buffer_t sdu)              = 0;
  virtual void write_pdu_bcch_bch(srsran::unique_byte_buffer_t sdu)                    = 0;
  virtual void write_pdu_bcch_dlsch(srsran::unique_byte_buffer_t sdu)                  = 0;
  virtual void write_pdu_pcch(srsran::unique_byte_buffer_t sdu)                        = 0;
  virtual void write_pdu_mch(uint32_t lcid, srsran::unique_byte_buffer_t sdu)          = 0;
  virtual void notify_delivery(uint32_t lcid, const srsran::pdcp_sn_vector_t& pdcp_sn) = 0;
  virtual void notify_failure(uint32_t lcid, const srsran::pdcp_sn_vector_t& pdcp_sn)  = 0;
};

class pdcp_interface_gw
{
public:
  virtual void write_sdu(uint32_t lcid, srsran::unique_byte_buffer_t sdu) = 0;
  virtual bool is_lcid_enabled(uint32_t lcid)                             = 0;
};

// STACK interface for GW
class stack_interface_gw : public pdcp_interface_gw
{
public:
  virtual bool is_registered()         = 0;
  virtual bool start_service_request() = 0;
};

} // namespace srsue

#endif // SRSRAN_UE_PDCP_INTERFACES_H
