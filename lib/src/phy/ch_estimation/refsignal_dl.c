/**
 * Copyright 2013-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsran/srsran.h"
#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "srsran/phy/ch_estimation/refsignal_dl.h"
#include "srsran/phy/common/phy_common.h"
#include "srsran/phy/common/sequence.h"
#include "srsran/phy/utils/debug.h"
#include "srsran/phy/utils/vector.h"

/** Allocates memory for the 20 slots in a subframe
 */
int srsran_refsignal_cs_init(srsran_refsignal_t* q, uint32_t max_prb)
{
  int ret = SRSRAN_ERROR_INVALID_INPUTS;

  if (q != NULL) {
    ret = SRSRAN_ERROR;
    bzero(q, sizeof(srsran_refsignal_t));
    for (int p = 0; p < 2; p++) {
      for (int i = 0; i < SRSRAN_NOF_SF_X_FRAME; i++) {
        q->pilots[p][i] = srsran_vec_cf_malloc(SRSRAN_REFSIGNAL_MAX_NUM_SF_MBSFN(max_prb));
        if (!q->pilots[p][i]) {
          perror("malloc");
          goto free_and_exit;
        }
      }
    }
    ret = SRSRAN_SUCCESS;
  }
free_and_exit:
  if (ret == SRSRAN_ERROR) {
    srsran_refsignal_free(q);
  }
  return ret;
}

/** Allocates and precomputes the Cell-Specific Reference (CSR) signal for
 * the 20 slots in a subframe
 */
int srsran_refsignal_cs_set_cell(srsran_refsignal_t* q, srsran_cell_t cell)
{
  uint32_t          c_init;
  uint32_t          N_cp, mp;
  srsran_sequence_t seq;
  int               ret = SRSRAN_ERROR_INVALID_INPUTS;

  if (q != NULL && srsran_cell_isvalid(&cell)) {
    if (cell.id != q->cell.id || q->cell.nof_prb == 0) {
      q->cell = cell;

      bzero(&seq, sizeof(srsran_sequence_t));
      if (srsran_sequence_init(&seq, 2 * 2 * SRSRAN_MAX_PRB)) {
        return SRSRAN_ERROR;
      }

      if (SRSRAN_CP_ISNORM(cell.cp)) {
        N_cp = 1;
      } else {
        N_cp = 0;
      }

      srsran_dl_sf_cfg_t sf_cfg;
      ZERO_OBJECT(sf_cfg);

      for (uint32_t ns = 0; ns < SRSRAN_NSLOTS_X_FRAME; ns++) {
        for (uint32_t p = 0; p < 2; p++) {
          sf_cfg.tti        = ns / 2;
          uint32_t nsymbols = srsran_refsignal_cs_nof_symbols(q, &sf_cfg, 2 * p) / 2;
          for (uint32_t l = 0; l < nsymbols; l++) {
            /* Compute sequence init value */
            uint32_t lp = srsran_refsignal_cs_nsymbol(l, cell.cp, 2 * p);
            c_init      = 1024 * (7 * (ns + 1) + lp + 1) * (2 * cell.id + 1) + 2 * cell.id + N_cp;

            /* generate sequence for this symbol and slot */
            srsran_sequence_set_LTE_pr(&seq, 2 * 2 * SRSRAN_MAX_PRB, c_init);

            /* Compute signal */
            for (uint32_t i = 0; i < 2 * q->cell.nof_prb; i++) {
              uint32_t idx = SRSRAN_REFSIGNAL_PILOT_IDX(i, (ns % 2) * nsymbols + l, q->cell);
              mp           = i + SRSRAN_MAX_PRB - cell.nof_prb;
              /* save signal */
              __real__ q->pilots[p][ns / 2][idx] = (1 - 2 * (float)seq.c[2 * mp + 0]) * M_SQRT1_2;
              __imag__ q->pilots[p][ns / 2][idx] = (1 - 2 * (float)seq.c[2 * mp + 1]) * M_SQRT1_2;
            }
          }
        }
      }
      srsran_sequence_free(&seq);
    }
    ret = SRSRAN_SUCCESS;
  }
  return ret;
}

/** Deallocates a srsran_refsignal_cs_t object allocated with srsran_refsignal_cs_init */
void srsran_refsignal_free(srsran_refsignal_t* q)
{
  for (int p = 0; p < 2; p++) {
    for (int i = 0; i < SRSRAN_NOF_SF_X_FRAME; i++) {
      if (q->pilots[p][i]) {
        free(q->pilots[p][i]);
      }
    }
  }
  bzero(q, sizeof(srsran_refsignal_t));
}

uint32_t srsran_refsignal_cs_v(uint32_t port_id, uint32_t ref_symbol_idx)
{
  uint32_t v = 0;
  switch (port_id) {
    case 0:
      if (!(ref_symbol_idx % 2)) {
        v = 0;
      } else {
        v = 3;
      }
      break;
    case 1:
      if (!(ref_symbol_idx % 2)) {
        v = 3;
      } else {
        v = 0;
      }
      break;
    case 2:
      if (ref_symbol_idx == 0) {
        v = 0;
      } else {
        v = 3;
      }
      break;
    case 3:
      if (ref_symbol_idx == 0) {
        v = 3;
      } else {
        v = 0;
      }
      break;
  }
  return v;
}

inline uint32_t srsran_refsignal_cs_nof_symbols(srsran_refsignal_t* q, srsran_dl_sf_cfg_t* sf, uint32_t port_id)
{
  if (q == NULL || sf == NULL || q->cell.frame_type == SRSRAN_FDD || !sf->tdd_config.configured ||
      srsran_sfidx_tdd_type(sf->tdd_config, sf->tti % 10) == SRSRAN_TDD_SF_D) {
    if (port_id < 2) {
      return 4;
    } else {
      return 2;
    }
  } else {
    uint32_t nof_dw_symbols = srsran_sfidx_tdd_nof_dw(sf->tdd_config);
    if (q->cell.cp == SRSRAN_CP_NORM) {
      if (nof_dw_symbols >= 12) {
        if (port_id < 2) {
          return 4;
        } else {
          return 2;
        }
      } else if (nof_dw_symbols >= 9) {
        if (port_id < 2) {
          return 3;
        } else {
          return 2;
        }
      } else if (nof_dw_symbols >= 5) {
        if (port_id < 2) {
          return 2;
        } else {
          return 1;
        }
      } else {
        return 1;
      }
    } else {
      if (nof_dw_symbols >= 10) {
        if (port_id < 2) {
          return 4;
        } else {
          return 2;
        }
      } else if (nof_dw_symbols >= 8) {
        if (port_id < 2) {
          return 3;
        } else {
          return 2;
        }
      } else if (nof_dw_symbols >= 4) {
        if (port_id < 2) {
          return 2;
        } else {
          return 1;
        }
      } else {
        return 1;
      }
    }
  }
}

inline uint32_t srsran_refsignal_cs_nof_pilots_x_slot(uint32_t nof_ports)
{
  switch (nof_ports) {
    case 2:
      return 8;
    case 4:
      return 12;
    default:
      return 4;
  }
}

inline uint32_t srsran_refsignal_cs_nof_re(srsran_refsignal_t* q, srsran_dl_sf_cfg_t* sf, uint32_t port_id)
{
  uint32_t nof_re = srsran_refsignal_cs_nof_symbols(q, sf, port_id);
  if (q != NULL) {
    nof_re *= q->cell.nof_prb * 2; // 2 RE per PRB
  }
  return nof_re;
}

inline uint32_t srsran_refsignal_cs_fidx(srsran_cell_t cell, uint32_t l, uint32_t port_id, uint32_t m)
{
  return 6 * m + ((srsran_refsignal_cs_v(port_id, l) + (cell.id % 6)) % 6);
}

inline uint32_t srsran_refsignal_cs_nsymbol(uint32_t l, srsran_cp_t cp, uint32_t port_id)
{
  if (port_id < 2) {
    if (l % 2) {
      return (l / 2 + 1) * SRSRAN_CP_NSYMB(cp) - 3;
    } else {
      return (l / 2) * SRSRAN_CP_NSYMB(cp);
    }
  } else {
    return 1 + l * SRSRAN_CP_NSYMB(cp);
  }
}

/* Maps a reference signal initialized with srsran_refsignal_cs_init() into an array of subframe symbols */
int srsran_refsignal_cs_put_sf(srsran_refsignal_t* q, srsran_dl_sf_cfg_t* sf, uint32_t port_id, cf_t* sf_symbols)
{
  uint32_t i, l;
  uint32_t fidx;

  if (q != NULL && port_id < SRSRAN_MAX_PORTS && sf_symbols != NULL) {
    cf_t* pilots = q->pilots[port_id / 2][sf->tti % 10];
    for (l = 0; l < srsran_refsignal_cs_nof_symbols(q, sf, port_id); l++) {
      uint32_t nsymbol = srsran_refsignal_cs_nsymbol(l, q->cell.cp, port_id);
      /* Compute offset frequency index */
      fidx = ((srsran_refsignal_cs_v(port_id, l) + (q->cell.id % 6)) % 6);
      for (i = 0; i < 2 * q->cell.nof_prb; i++) {
        sf_symbols[SRSRAN_RE_IDX(q->cell.nof_prb, nsymbol, fidx)] = pilots[SRSRAN_REFSIGNAL_PILOT_IDX(i, l, q->cell)];
        fidx += SRSRAN_NRE / 2; // 1 reference every 6 RE
      }
    }
    return SRSRAN_SUCCESS;
  } else {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
}

/** Copies the RE containing references from an array of subframe symbols to the pilots array. */
int srsran_refsignal_cs_get_sf(srsran_refsignal_t* q,
                               srsran_dl_sf_cfg_t* sf,
                               uint32_t            port_id,
                               cf_t*               sf_symbols,
                               cf_t*               pilots)
{
  uint32_t i, l;
  uint32_t fidx;

  if (q != NULL && pilots != NULL && sf_symbols != NULL) {
    for (l = 0; l < srsran_refsignal_cs_nof_symbols(q, sf, port_id); l++) {
      uint32_t nsymbol = srsran_refsignal_cs_nsymbol(l, q->cell.cp, port_id);
      /* Compute offset frequency index */
      fidx = srsran_refsignal_cs_fidx(q->cell, l, port_id, 0);
      for (i = 0; i < 2 * q->cell.nof_prb; i++) {
        pilots[SRSRAN_REFSIGNAL_PILOT_IDX(i, l, q->cell)] = sf_symbols[SRSRAN_RE_IDX(q->cell.nof_prb, nsymbol, fidx)];
        fidx += SRSRAN_NRE / 2; // 2 references per PRB
      }
    }
    return SRSRAN_SUCCESS;
  } else {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
}

SRSRAN_API int srsran_refsignal_mbsfn_put_sf(srsran_cell_t cell,
                                             uint32_t      port_id,
                                             cf_t*         cs_pilots,
                                             cf_t*         mbsfn_pilots,
                                             cf_t*         sf_symbols)
{
  uint32_t i, l;
  uint32_t fidx;

  if (srsran_cell_isvalid(&cell) && srsran_portid_isvalid(port_id) && cs_pilots != NULL && mbsfn_pilots != NULL &&
      sf_symbols != NULL) {
    // adding CS refs for the non-mbsfn section of the sub-frame
    fidx = ((srsran_refsignal_cs_v(port_id, 0) + (cell.id % 6)) % 6);
    for (i = 0; i < 2 * cell.nof_prb; i++) {
      sf_symbols[SRSRAN_RE_IDX(cell.nof_prb, 0, fidx)] = cs_pilots[SRSRAN_REFSIGNAL_PILOT_IDX(i, 0, cell)];
      fidx += SRSRAN_NRE / 2; // 1 reference every 6 RE
    }

    for (l = 0; l < srsran_refsignal_mbsfn_nof_symbols(); l++) {
      uint32_t nsymbol = srsran_refsignal_mbsfn_nsymbol(l, SRSRAN_SCS_15KHZ);
      fidx             = srsran_refsignal_mbsfn_fidx(l, SRSRAN_SCS_15KHZ);
      for (i = 0; i < 6 * cell.nof_prb; i++) {
        sf_symbols[SRSRAN_RE_IDX(cell.nof_prb, nsymbol, fidx)] =
            mbsfn_pilots[SRSRAN_REFSIGNAL_PILOT_IDX_MBSFN(i, l, cell, SRSRAN_SCS_15KHZ)];
        fidx += SRSRAN_NRE / 6;
      }
    }

    return SRSRAN_SUCCESS;
  } else {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
}

uint32_t srsran_refsignal_mbsfn_nof_symbols(srsran_scs_t scs)
{
  switch (scs) {
    case SRSRAN_SCS_15KHZ:  return 3;
    case SRSRAN_SCS_7KHZ5:  return 3;
    case SRSRAN_SCS_2KHZ5: return 2;
    case SRSRAN_SCS_1KHZ25: return 1;
    case SRSRAN_SCS_0KHZ37: return 1;
    default: return 0;
  }
}

uint32_t srsran_refsignal_mbsfn_rs_per_symbol(srsran_scs_t scs)
{
  switch (scs) {
    case SRSRAN_SCS_15KHZ:  return 6;
    case SRSRAN_SCS_7KHZ5:  return 6;
    case SRSRAN_SCS_2KHZ5: return 18;
    case SRSRAN_SCS_1KHZ25: return 24;
    case SRSRAN_SCS_0KHZ37: return 40; // [kku] add type 2
    default: return 0;
  }
}

uint32_t srsran_refsignal_mbsfn_rs_per_rb(srsran_scs_t scs)
{
  return srsran_refsignal_mbsfn_nof_symbols(scs) * srsran_refsignal_mbsfn_rs_per_symbol(scs);
}

uint32_t srsran_symbols_per_mbsfn_subframe(srsran_scs_t scs)
{
  switch (scs) {
    case SRSRAN_SCS_15KHZ:  return 6;
    case SRSRAN_SCS_7KHZ5:  return 3;
    case SRSRAN_SCS_2KHZ5: return 1;
    case SRSRAN_SCS_1KHZ25: return 1;
    case SRSRAN_SCS_0KHZ37: return 1;
    default: return 0;
  }
}

inline uint32_t srsran_refsignal_mbsfn_fidx(uint32_t l, srsran_scs_t scs)
{
  uint32_t ret = 0;
    switch (scs) {
    case SRSRAN_SCS_15KHZ:
      if (l == 0) {
        ret = 0;
      } else if (l == 1) {
        ret = 1;
      } else if (l == 2) {
        ret = 0;
      }
      break;
    case SRSRAN_SCS_7KHZ5:
      if (l == 0) {
        ret = 0;
      } else if (l == 1) {
        ret = 2;
      } else if (l == 2) {
        ret = 0;
      }
      break;
    case SRSRAN_SCS_2KHZ5:
      if (l == 0) {
        ret = 0;
      } else {
        ret = 2;
      }
      break;
    case SRSRAN_SCS_1KHZ25:
    case SRSRAN_SCS_0KHZ37:
      ret = 0;
      break;
  }

  return ret;
}

inline uint32_t srsran_refsignal_mbsfn_offset(uint32_t l, uint32_t s, uint32_t sf, srsran_scs_t scs)
{
  uint32_t ret = 0;

  switch (scs) {
    case SRSRAN_SCS_15KHZ:
      if (s == 1 && l == 0) {
        ret = 1;
      }
      break;
    case SRSRAN_SCS_7KHZ5:
      if (s == 1 && l == 0) {
        ret = 2;
      }
      break;
    case SRSRAN_SCS_2KHZ5:
      if (s == 1) {
        ret = 2;
      }
      break;
    case SRSRAN_SCS_1KHZ25:
      if (sf%2 != 0) {
        ret = 3;
      }
      break;
    case SRSRAN_SCS_0KHZ37:
        ret = 0; // [kku]
        break;
  }
  return ret;
}

inline uint32_t srsran_refsignal_mbsfn_nsymbol(uint32_t l, srsran_scs_t scs)
{
  uint32_t ret = 0;

  switch (scs) {
    case SRSRAN_SCS_15KHZ:
      if (l == 0) {
        ret = 2;
      } else if (l == 1) {
        ret = 6;
      } else if (l == 2) {
        ret = 10;
      }
      break;
    case SRSRAN_SCS_7KHZ5:
      if (l == 0) {
        ret = 1;
      } else if (l == 1) {
        ret = 3;
      } else if (l == 2) {
        ret = 5;
      }
      break;
    case SRSRAN_SCS_2KHZ5:
      ret = l;
      break;
    case SRSRAN_SCS_1KHZ25:
    case SRSRAN_SCS_0KHZ37:
      ret = 0;
      break;
  }

  return ret;
}

int srsran_refsignal_mbsfn_gen_seq(srsran_refsignal_t* q, srsran_cell_t cell, uint32_t N_mbsfn_id, srsran_scs_t scs)
{
  uint32_t c_init;
  uint32_t i, ns, l, p;
  uint32_t mp;
  int      ret = SRSRAN_ERROR;

  srsran_sequence_t seq_mbsfn;
  bzero(&seq_mbsfn, sizeof(srsran_sequence_t));
  if (srsran_sequence_init(&seq_mbsfn, 20 * SRSRAN_REFSIGNAL_NUM_SF_MBSFN(SRSRAN_MAX_PRB, scs))) {
    goto free_and_exit;
  }

  for (ns = 0; ns < SRSRAN_NOF_SF_X_FRAME; ns++) {
    for (p = 0; p < 2; p++) {
      uint32_t nsymbols = srsran_refsignal_mbsfn_nof_symbols(scs);
      for (l = 0; l < nsymbols; l++) {
        uint32_t lp   = (srsran_refsignal_mbsfn_nsymbol(l, scs)) % srsran_symbols_per_mbsfn_subframe(scs);
        uint32_t slot = (l) ? (ns * 2 + 1) : (ns * 2);

        if (scs == SRSRAN_SCS_1KHZ25) {
          slot = ns;
          lp = l;
        } else if (scs == SRSRAN_SCS_2KHZ5) {
          slot = ns;
          lp = l;
        }

        c_init        = 512 *
          (7 * (slot + 1) + lp + 1) *
          (2 * N_mbsfn_id + 1) + N_mbsfn_id;

        srsran_sequence_set_LTE_pr(&seq_mbsfn, 10 * SRSRAN_REFSIGNAL_NUM_SF_MBSFN(SRSRAN_MAX_PRB, scs), c_init);
        for (i = 0; i < srsran_refsignal_mbsfn_rs_per_symbol(scs) * q->cell.nof_prb; i++) {
          uint32_t idx                   = SRSRAN_REFSIGNAL_PILOT_IDX_MBSFN(i, l, q->cell, scs);
          float delta = (SRSRAN_MAX_PRB - cell.nof_prb) / 2.0;
          if (scs == SRSRAN_SCS_2KHZ5) {
            mp                           = i + ((float)SRSRAN_NRE_SCS(scs) / 4.0) * delta;
          } else {
            mp                           = i + 3 * (SRSRAN_MAX_PRB - cell.nof_prb);
          }
          __real__ q->pilots[p][ns][idx] = (1 - 2 * (float)seq_mbsfn.c[2 * mp + 0]) * M_SQRT1_2;
          __imag__ q->pilots[p][ns][idx] = (1 - 2 * (float)seq_mbsfn.c[2 * mp + 1]) * M_SQRT1_2;
          //TRACE("p %d ns %d idx %d (l %d, i %d, mp %d): %f + %f i", p, ns, idx, l, i, mp,
          //    creal(q->pilots[p][ns][idx]),
          //    cimag(q->pilots[p][ns][idx]));
        }
      }
    }
  }

  srsran_sequence_free(&seq_mbsfn);
  ret = SRSRAN_SUCCESS;

free_and_exit:
  if (ret == SRSRAN_ERROR) {
    srsran_sequence_free(&seq_mbsfn);
    srsran_refsignal_free(q);
  }
  return ret;
}

int srsran_refsignal_mbsfn_init(srsran_refsignal_t* q, uint32_t max_prb, srsran_scs_t scs)
{
  int      ret = SRSRAN_ERROR_INVALID_INPUTS;
  uint32_t i, p;
  if (q != NULL) {
    ret = SRSRAN_ERROR;
    bzero(q, sizeof(srsran_refsignal_t));

    q->type = SRSRAN_SF_MBSFN;

    for (p = 0; p < 2; p++) {
      for (i = 0; i < SRSRAN_NOF_SF_X_FRAME; i++) {
        q->pilots[p][i] = srsran_vec_cf_malloc(max_prb * srsran_refsignal_mbsfn_rs_per_rb(scs));
        if (!q->pilots[p][i]) {
          perror("malloc");
          goto free_and_exit;
        }
      }
    }

    ret = SRSRAN_SUCCESS;
  }

free_and_exit:
  if (ret == SRSRAN_ERROR) {
    srsran_refsignal_free(q);
  }
  return ret;
}

int srsran_refsignal_mbsfn_set_cell(srsran_refsignal_t* q, srsran_cell_t cell, uint16_t mbsfn_area_id, srsran_scs_t scs)
{
  int ret = SRSRAN_SUCCESS;

  if (q == NULL) {
    ret = SRSRAN_ERROR_INVALID_INPUTS;
    goto exit;
  }

  q->cell          = cell;
  q->mbsfn_area_id = mbsfn_area_id;
  if (srsran_refsignal_mbsfn_gen_seq(q, q->cell, q->mbsfn_area_id, scs) < SRSRAN_SUCCESS) {
    ret = SRSRAN_ERROR;
    goto exit;
  }

exit:
  return ret;
}

int srsran_refsignal_mbsfn_get_sf(srsran_cell_t cell, uint32_t port_id, cf_t* sf_symbols, cf_t* pilots, srsran_scs_t scs, uint32_t sf_idx)
{
  uint32_t i, l;
  uint32_t fidx;
  uint32_t nsymbol;
  uint32_t nonmbsfn_offset = 0;

  if (srsran_cell_isvalid(&cell) && srsran_portid_isvalid(port_id) && pilots != NULL && sf_symbols != NULL) {
    if (scs == SRSRAN_SCS_15KHZ) {
      // getting refs from non mbsfn section of subframe
      nsymbol = srsran_refsignal_cs_nsymbol(0, cell.cp, port_id);
      fidx    = ((srsran_refsignal_cs_v(port_id, 0) + (cell.id % 6)) % 6);
      for (i = 0; i < 2 * cell.nof_prb; i++) {
        pilots[SRSRAN_REFSIGNAL_PILOT_IDX(i, 0, cell)] = sf_symbols[SRSRAN_RE_IDX(cell.nof_prb, nsymbol, fidx)];
        fidx += SRSRAN_NRE / 2; // 2 references per PRB
      }
      nonmbsfn_offset = 2 * cell.nof_prb;
    }

    for (l = 0; l < srsran_refsignal_mbsfn_nof_symbols(scs); l++) {
      nsymbol = srsran_refsignal_mbsfn_nsymbol(l, scs);
      if (scs == SRSRAN_SCS_1KHZ25) {
        fidx    = sf_idx%2==0 ? 0 : 3;
      } else {
        fidx    = srsran_refsignal_mbsfn_fidx(l, scs);
      }
      for (i = 0; i < srsran_refsignal_mbsfn_rs_per_symbol(scs) * cell.nof_prb; i++) {
        pilots[SRSRAN_REFSIGNAL_PILOT_IDX_MBSFN(i, l, cell, scs) + nonmbsfn_offset] =
          sf_symbols[SRSRAN_RE_IDX_MBSFN(cell.nof_prb, nsymbol, fidx, scs)];
      //ERROR("Reading RS %d from sf %d %d,%d: %f + %f i", i, sf_idx, nsymbol, fidx, 
      //    creal(sf_symbols[SRSRAN_RE_IDX_MBSFN(cell.nof_prb, nsymbol, fidx, scs)]),
      //    cimag(sf_symbols[SRSRAN_RE_IDX_MBSFN(cell.nof_prb, nsymbol, fidx, scs)]));
        fidx += SRSRAN_NRE_SCS(scs) / srsran_refsignal_mbsfn_rs_per_symbol(scs);
      }
    }

    return SRSRAN_SUCCESS;
  } else {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }
}
