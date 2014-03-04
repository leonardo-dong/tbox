/*!The Treasure Box Library
 * 
 * TBox is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 * 
 * TBox is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with TBox; 
 * If not, see <a href="http://www.gnu.org/licenses/"> http://www.gnu.org/licenses/</a>
 * 
 * Copyright (C) 2009 - 2012, ruki All rights reserved.
 *
 * @author		ruki
 * @file		tstream.c
 * @ingroup 	stream
 *
 */

/* ///////////////////////////////////////////////////////////////////////
 * trace
 */
#define TB_TRACE_IMPL_TAG 				"tstream"
 
/* ///////////////////////////////////////////////////////////////////////
 * includes
 */
#include "tstream.h"
#include "../network/network.h"
#include "../platform/platform.h"

/* ///////////////////////////////////////////////////////////////////////
 * types
 */

// the tstream open type
typedef struct __tb_tstream_open_t
{
	// the func
	tb_tstream_open_func_t 	func;

	// the priv
	tb_pointer_t 			priv;

}tb_tstream_open_t;

// the tstream save type
typedef struct __tb_tstream_save_t
{
	// the func
	tb_tstream_save_func_t 	func;

	// the priv
	tb_pointer_t 			priv;

}tb_tstream_save_t;

// the tstream osave type
typedef struct __tb_tstream_osave_t
{
	// the func
	tb_tstream_save_func_t 	func;

	// the priv
	tb_pointer_t 			priv;

	// the tstream
	tb_handle_t 			tstream;

}tb_tstream_osave_t;

// the tstream type
typedef struct __tb_tstream_t
{
	// the istream
	tb_handle_t 			istream;

	// the ostream
	tb_handle_t 			ostream;

	// the istream is owner?
	tb_uint8_t 				iowner : 1;

	// the ostream is owner?
	tb_uint8_t 				oowner : 1;

	// is stoped?
	tb_atomic_t 			stoped;

	// is opened?
	tb_atomic_t 			opened;

	// is paused?
	tb_atomic_t 			paused;

	// is pausing?
	tb_atomic_t 			pausing;

	// the base time
	tb_hong_t 				base;

	// the base time for 1s
	tb_hong_t 				base1s;

	// the seek offset
	tb_hize_t 				offset;

	// the saved size
	tb_hize_t 				save;

	// the saved size for 1s
	tb_size_t 				save1s;
 
	// the limit rate
	tb_atomic_t 			lrate;

	// the current rate
	tb_size_t 				crate;

	// the func
	union
	{
		tb_tstream_open_t 	open;
		tb_tstream_save_t 	save;

	} func;

	// open and save
	tb_tstream_osave_t 		osave;

}tb_tstream_t;
 
/* ///////////////////////////////////////////////////////////////////////
 * implementation
 */
static tb_bool_t tb_tstream_istream_read_func(tb_astream_t* astream, tb_size_t state, tb_byte_t const* data, tb_size_t real, tb_size_t size, tb_pointer_t priv);
static tb_bool_t tb_tstream_ostream_writ_func(tb_astream_t* astream, tb_size_t state, tb_byte_t const* data, tb_size_t real, tb_size_t size, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)priv;
	tb_assert_and_check_return_val(astream && tstream && tstream->istream && tstream->func.save.func, tb_false);

	// trace
	tb_trace_impl("writ: real: %lu, size: %lu, state: %s", real, size, tb_stream_state_cstr(state));

	// the time
	tb_hong_t time = tb_aicp_time(tb_astream_aicp(astream));

	// done
	tb_bool_t bwrit = tb_false;
	do
	{
		// ok?
		tb_check_break(state == TB_STREAM_STATE_OK);
			
		// done func at first once
		if (!tstream->save && !tstream->func.save.func(state, tb_stream_offset(tstream->istream), tb_stream_size(tstream->istream), 0, 0, tstream->func.save.priv)) break;

		// save size
		tstream->save += real;

		// < 1s?
		tb_size_t delay = 0;
		tb_size_t lrate = tb_atomic_get(&tstream->lrate);
		if (time < tstream->base1s + 1000)
		{
			// save size for 1s
			tstream->save1s += real;

			// save current rate if < 1s from base
			if (time < tstream->base + 1000) tstream->crate = tstream->save1s;
					
			// compute the delay for limit rate
			if (lrate) delay = tstream->save1s >= lrate? tstream->base1s + 1000 - time : 0;
		}
		else
		{
			// save current rate
			tstream->crate = tstream->save1s;

			// update base1s
			tstream->base1s = time;

			// reset size
			tstream->save1s = 0;

			// reset delay
			delay = 0;

			// done func
			if (!tstream->func.save.func(state, tb_stream_offset(tstream->istream), tb_stream_size(tstream->istream), tstream->save, tstream->crate, tstream->func.save.priv)) break;
		}

		// reset state
		state = TB_STREAM_STATE_UNKNOWN_ERROR;
			
		// stoped?
		if (tb_atomic_get(&tstream->stoped))
		{
			state = TB_STREAM_STATE_KILLED;
			break;
		}

		// not finished? continue to writ
		if (real < size) bwrit = tb_true;
		// pausing or paused?
		else if (tb_atomic_get(&tstream->pausing) || tb_atomic_get(&tstream->paused))
		{
			// paused
			tb_atomic_set(&tstream->paused, 1);
			
			// clear pausing
			tb_atomic_set0(&tstream->pausing);
	
			// done func
			if (!tstream->func.save.func(TB_STREAM_STATE_PAUSED, tb_stream_offset(tstream->istream), tb_stream_size(tstream->istream), tstream->save, 0, tstream->func.save.priv)) break;
		}
		// continue?
		else
		{
			// trace
			tb_trace_impl("delay: %lu ms", delay);

			// continue to read it
			if (!tb_astream_read_after(tstream->istream, delay, lrate, tb_tstream_istream_read_func, (tb_pointer_t)tstream)) break;
		}

		// ok
		state = TB_STREAM_STATE_OK;

	} while (0);

	// failed? 
	if (state != TB_STREAM_STATE_OK) 
	{
		// compute the total rate
		tb_size_t trate = (tstream->save && (time > tstream->base))? (tb_size_t)((tstream->save * 1000) / (time - tstream->base)) : (tb_size_t)tstream->save;

		// done func
		tstream->func.save.func(state, tb_stream_offset(tstream->istream), tb_stream_size(tstream->istream), tstream->save, trate, tstream->func.save.priv);

		// break;
		bwrit = tb_false;
	}

	// continue to writ or break it
	return bwrit;
}
static tb_bool_t tb_tstream_ostream_sync_func(tb_astream_t* astream, tb_size_t state, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)priv;
	tb_assert_and_check_return_val(astream && tstream && tstream->istream && tstream->func.save.func, tb_false);

	// trace
	tb_trace_impl("sync: state: %s", tb_stream_state_cstr(state));

	// the time
	tb_hong_t time = tb_aicp_time(tb_astream_aicp(astream));

	// compute the total rate
	tb_size_t trate = (tstream->save && (time > tstream->base))? (tb_size_t)((tstream->save * 1000) / (time - tstream->base)) : (tb_size_t)tstream->save;

	// done func
	return tstream->func.save.func(state == TB_STREAM_STATE_OK? TB_STREAM_STATE_CLOSED : state, tb_stream_offset(tstream->istream), tb_stream_size(tstream->istream), tstream->save, trate, tstream->func.save.priv);
}
static tb_bool_t tb_tstream_istream_read_func(tb_astream_t* astream, tb_size_t state, tb_byte_t const* data, tb_size_t real, tb_size_t size, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)priv;
	tb_assert_and_check_return_val(astream && tstream && tstream->ostream && tstream->func.save.func, tb_false);

	// trace
	tb_trace_impl("read: size: %lu, state: %s", real, tb_stream_state_cstr(state));

	// done
	tb_bool_t bread = tb_false;
	do
	{
		// ok?
		tb_check_break(state == TB_STREAM_STATE_OK);

		// reset state
		state = TB_STREAM_STATE_UNKNOWN_ERROR;

		// stoped?
		if (tb_atomic_get(&tstream->stoped))
		{
			state = TB_STREAM_STATE_KILLED;
			break;
		}

		// check
		tb_assert_and_check_break(data && real);

		// for astream
		if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO)
		{
			// writ it
			if (!tb_astream_writ(tstream->ostream, data, real, tb_tstream_ostream_writ_func, tstream)) break;
		}
		// for gstream
		else if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AIOO)
		{
			// writ it
			if (!tb_gstream_bwrit(tstream->ostream, data, real)) break;

			// the time
			tb_hong_t time = tb_aicp_time(tb_astream_aicp(astream));

			// save size 
			tstream->save += real;

			// < 1s?
			tb_size_t delay = 0;
			tb_size_t lrate = tb_atomic_get(&tstream->lrate);
			if (time < tstream->base1s + 1000)
			{
				// save size for 1s
				tstream->save1s += real;

				// save current rate if < 1s from base
				if (time < tstream->base + 1000) tstream->crate = tstream->save1s;
			
				// compute the delay for limit rate
				if (lrate) delay = tstream->save1s >= lrate? tstream->base1s + 1000 - time : 0;
			}
			else
			{
				// save current rate
				tstream->crate = tstream->save1s;

				// update base1s
				tstream->base1s = time;

				// reset size
				tstream->save1s = 0;

				// reset delay
				delay = 0;
			}

			// done func
			if (!tstream->func.save.func(TB_STREAM_STATE_OK, tb_stream_offset(astream), tb_stream_size(astream), tstream->save, tstream->crate, tstream->func.save.priv)) break;

			// pausing or paused?
			if (tb_atomic_get(&tstream->pausing) || tb_atomic_get(&tstream->paused))
			{
				// paused
				tb_atomic_set(&tstream->paused, 1);
				
				// clear pausing
				tb_atomic_set0(&tstream->pausing);
		
				// done func
				if (!tstream->func.save.func(TB_STREAM_STATE_PAUSED, tb_stream_offset(astream), tb_stream_size(astream), tstream->save, 0, tstream->func.save.priv)) break;
			}
			// continue?
			else
			{
				// no delay? continue to read it immediately
				if (!delay) bread = tb_true;
				else 
				{
					// trace
					tb_trace_impl("delay: %lu ms", delay);

					// continue to read it after the delay time
					if (!tb_astream_read_after(tstream->istream, delay, lrate, tb_tstream_istream_read_func, tstream)) break;
				}
			}
		}
		else 
		{
			tb_assert_and_check_break(0);
		}

		// ok
		state = TB_STREAM_STATE_OK;

	} while (0);

	// closed or failed?
	if (state != TB_STREAM_STATE_OK) 
	{
		// sync it if closed
		tb_bool_t bend = tb_true;
		if (state == TB_STREAM_STATE_CLOSED)
		{
			if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AIOO)
				tb_gstream_bfwrit(tstream->ostream, tb_null, 0);
			else if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO)
				bend = tb_astream_sync(tstream->ostream, tb_true, tb_tstream_ostream_sync_func, tstream)? tb_false : tb_true;
		}

		// end? 
		if (bend)
		{
			// the time
			tb_hong_t time = tb_aicp_time(tb_astream_aicp(astream));

			// compute the total rate
			tb_size_t trate = (tstream->save && (time > tstream->base))? (tb_size_t)((tstream->save * 1000) / (time - tstream->base)) : (tb_size_t)tstream->save;

			// done func
			tstream->func.save.func(state, tb_stream_offset(astream), tb_stream_size(astream), tstream->save, trate, tstream->func.save.priv);
		}

		// break
		bread = tb_false;
	}

	// continue to read or break it
	return bread;
}
static tb_bool_t tb_tstream_istream_open_func(tb_astream_t* astream, tb_size_t state, tb_hize_t offset, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)priv;
	tb_assert_and_check_return_val(astream && tstream && tstream->func.open.func, tb_false);

	// trace
	tb_trace_impl("open: istream: offset: %llu, state: %s", offset, tb_stream_state_cstr(state));

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok?
		tb_check_break(state == TB_STREAM_STATE_OK);

		// reset state
		state = TB_STREAM_STATE_UNKNOWN_ERROR;
			
		// stoped?
		if (tb_atomic_get(&tstream->stoped))
		{
			state = TB_STREAM_STATE_KILLED;
			break;
		}

		// opened
		tb_atomic_set(&tstream->opened, 1);

		// done func
		ok = tstream->func.open.func(TB_STREAM_STATE_OK, tb_stream_offset(astream), tb_stream_size(astream), tstream->func.open.priv);

		// ok
		state = TB_STREAM_STATE_OK;

	} while (0);

	// failed?
	if (state != TB_STREAM_STATE_OK) 
	{
		// done func
		ok = tstream->func.open.func(state, 0, 0, tstream->func.open.priv);

		// stoped
		tb_atomic_set(&tstream->stoped, 1);
	}

	// ok?
	return ok;
}
static tb_bool_t tb_tstream_ostream_open_func(tb_astream_t* astream, tb_size_t state, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)priv;
	tb_assert_and_check_return_val(astream && tstream && tstream->func.open.func, tb_false);

	// trace
	tb_trace_impl("open: ostream: state: %s", tb_stream_state_cstr(state));

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok?
		tb_check_break(state == TB_STREAM_STATE_OK);

		// reset state
		state = TB_STREAM_STATE_UNKNOWN_ERROR;

		// check
		tb_assert_and_check_break(tstream->istream);

		// stoped?
		if (tb_atomic_get(&tstream->stoped))
		{
			state = TB_STREAM_STATE_KILLED;
			break;
		}

		// open and seek istream
		if (!tb_astream_oseek(tstream->istream, tstream->offset, tb_tstream_istream_open_func, tstream)) break;

		// ok
		state = TB_STREAM_STATE_OK;

	} while (0);

	// failed?
	if (state != TB_STREAM_STATE_OK) 
	{
		// done func
		ok = tstream->func.open.func(state, 0, 0, tstream->func.open.priv);

		// stoped
		tb_atomic_set(&tstream->stoped, 1);
	}

	// ok
	return ok;
}
static tb_bool_t tb_tstream_open_func(tb_size_t state, tb_hize_t offset, tb_hong_t size, tb_pointer_t priv)
{
	// check
	tb_tstream_osave_t* osave = (tb_tstream_osave_t*)priv;
	tb_assert_and_check_return_val(osave && osave->func, tb_false);

	// the tstream
	tb_tstream_t* tstream = (tb_tstream_t*)osave->tstream;
	tb_assert_and_check_return_val(tstream, tb_false);

	// trace
	tb_trace_impl("open: offset: %llu, size: %lld, state: %s", offset, size, tb_stream_state_cstr(state));

	// done
	tb_bool_t ok = tb_true;
	do
	{
		// ok? 
		tb_check_break(state == TB_STREAM_STATE_OK);

		// reset state
		state = TB_STREAM_STATE_UNKNOWN_ERROR;
		
		// stoped?
		if (tb_atomic_get(&tstream->stoped))
		{
			state = TB_STREAM_STATE_KILLED;
			break;
		}

		// save it
		if (!tb_tstream_save(tstream, osave->func, osave->priv)) break;

		// ok
		state = TB_STREAM_STATE_OK;

	} while (0);

	// failed? 
	if (state != TB_STREAM_STATE_OK) 
	{	
		// stoped
		tb_atomic_set(&tstream->stoped, 1);

		// done func
		ok = osave->func(state, 0, 0, 0, 0, osave->priv);
	}

	// ok?
	return ok;
}

/* ///////////////////////////////////////////////////////////////////////
 * interfaces
 */
tb_hong_t tb_tstream_save_gg(tb_gstream_t* istream, tb_gstream_t* ostream, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(ostream && istream, -1);	

	// open it first if istream have been not opened
	if (!tb_stream_is_opened(istream) && !tb_gstream_open(istream)) return -1;
	
	// open it first if ostream have been not opened
	if (!tb_stream_is_opened(ostream) && !tb_gstream_open(ostream)) return -1;
				
	// done func
	if (func) func(TB_STREAM_STATE_OK, tb_stream_offset(istream), tb_stream_size(istream), 0, 0, priv);

	// writ data
	tb_byte_t 	data[TB_GSTREAM_BLOCK_MAXN];
	tb_hize_t 	writ = 0;
	tb_hize_t 	left = tb_stream_left(istream);
	tb_hong_t 	base = tb_mclock();
	tb_hong_t 	base1s = base;
	tb_hong_t 	time = 0;
	tb_size_t 	crate = 0;
	tb_long_t 	delay = 0;
	tb_hize_t 	writ1s = 0;
	do
	{
		// the need
		tb_size_t need = lrate? tb_min(lrate, TB_GSTREAM_BLOCK_MAXN) : TB_GSTREAM_BLOCK_MAXN;

		// read data
		tb_long_t real = tb_gstream_aread(istream, data, need);
		if (real > 0)
		{
			// writ data
			if (!tb_gstream_bwrit(ostream, data, real)) break;

			// save writ
			writ += real;

			// has func or limit rate?
			if (func || lrate) 
			{
				// the time
				time = tb_mclock();

				// < 1s?
				if (time < base1s + 1000)
				{
					// save writ1s
					writ1s += real;

					// save current rate if < 1s from base
					if (time < base + 1000) crate = writ1s;
				
					// compute the delay for limit rate
					if (lrate) delay = writ1s >= lrate? base1s + 1000 - time : 0;
				}
				else
				{
					// save current rate
					crate = writ1s;

					// update base1s
					base1s = time;

					// reset writ1s
					writ1s = 0;

					// reset delay
					delay = 0;

					// done func
					if (func) func(TB_STREAM_STATE_OK, tb_stream_offset(istream), tb_stream_size(istream), writ, crate, priv);
				}

				// wait some time for limit rate
				if (delay) tb_msleep(delay);
			}
		}
		else if (!real) 
		{
			// wait
			tb_long_t wait = tb_gstream_wait(istream, TB_GSTREAM_WAIT_READ, tb_stream_timeout(istream));
			tb_assert_and_check_break(wait >= 0);

			// timeout?
			tb_check_break(wait);

			// has writ?
			tb_assert_and_check_break(wait & TB_GSTREAM_WAIT_READ);
		}
		else break;

		// is end?
		if (left && writ >= left) break;

	} while(1);

	// flush the ostream
	if (!tb_gstream_bfwrit(ostream, tb_null, 0)) return -1;

	// has func?
	if (func) 
	{
		// the time
		time = tb_mclock();

		// compute the total rate
		tb_size_t trate = (writ && (time > base))? (tb_size_t)((writ * 1000) / (time - base)) : writ;
	
		// done func
		func(TB_STREAM_STATE_CLOSED, tb_stream_offset(istream), tb_stream_size(istream), writ, trate, priv);
	}

	// ok?
	return writ;
}
tb_hong_t tb_tstream_save_gu(tb_gstream_t* istream, tb_char_t const* ourl, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(istream && ourl, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	ostream = tb_null;
	do
	{
		// init ostream
		ostream = tb_gstream_init_from_url(ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit ostream
	if (ostream) tb_gstream_exit(ostream);
	ostream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_gd(tb_gstream_t* istream, tb_byte_t* odata, tb_size_t osize, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(istream && odata && osize, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	ostream = tb_null;
	do
	{
		// init ostream
		ostream = tb_gstream_init_from_data(odata, osize);
		tb_assert_and_check_break(ostream);

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit ostream
	if (ostream) tb_gstream_exit(ostream);
	ostream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_uu(tb_char_t const* iurl, tb_char_t const* ourl, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(iurl && ourl, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	istream = tb_null;
	tb_gstream_t* 	ostream = tb_null;
	do
	{
		// init istream
		istream = tb_gstream_init_from_url(iurl);
		tb_assert_and_check_break(istream);

		// init ostream
		ostream = tb_gstream_init_from_url(ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit istream
	if (istream) tb_gstream_exit(istream);
	istream = tb_null;

	// exit ostream
	if (ostream) tb_gstream_exit(ostream);
	ostream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_ug(tb_char_t const* iurl, tb_gstream_t* ostream, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(iurl && ostream, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	istream = tb_null;
	do
	{
		// init istream
		istream = tb_gstream_init_from_url(iurl);
		tb_assert_and_check_break(istream);

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit istream
	if (istream) tb_gstream_exit(istream);
	istream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_ud(tb_char_t const* iurl, tb_byte_t* odata, tb_size_t osize, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(iurl && odata && osize, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	istream = tb_null;
	tb_gstream_t* 	ostream = tb_null;
	do
	{
		// init istream
		istream = tb_gstream_init_from_url(iurl);
		tb_assert_and_check_break(istream);

		// init ostream
		ostream = tb_gstream_init_from_data(odata, osize);
		tb_assert_and_check_break(ostream);

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit istream
	if (istream) tb_gstream_exit(istream);
	istream = tb_null;

	// exit ostream
	if (ostream) tb_gstream_exit(ostream);
	ostream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_du(tb_byte_t const* idata, tb_size_t isize, tb_char_t const* ourl, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(idata && isize && ourl, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	istream = tb_null;
	tb_gstream_t* 	ostream = tb_null;
	do
	{
		// init istream
		istream = tb_gstream_init_from_data(idata, isize);
		tb_assert_and_check_break(istream);

		// init ostream
		ostream = tb_gstream_init_from_url(ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit istream
	if (istream) tb_gstream_exit(istream);
	istream = tb_null;

	// exit ostream
	if (ostream) tb_gstream_exit(ostream);
	ostream = tb_null;

	// ok?
	return size;
}
tb_hong_t tb_tstream_save_dg(tb_byte_t const* idata, tb_size_t isize, tb_gstream_t* ostream, tb_size_t lrate, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_assert_and_check_return_val(idata && isize && ostream, -1);

	// done
	tb_hong_t 		size = -1;
	tb_gstream_t* 	istream = tb_null;
	do
	{
		// init istream
		istream = tb_gstream_init_from_data(idata, isize);
		tb_assert_and_check_break(istream);

		// save stream
		size = tb_tstream_save_gg(istream, ostream, lrate, func, priv);

	} while (0);

	// exit istream
	if (istream) tb_gstream_exit(istream);
	istream = tb_null;

	// ok?
	return size;
}
tb_handle_t tb_tstream_init_aa(tb_astream_t* istream, tb_astream_t* ostream, tb_hize_t offset)
{
	// done
	tb_tstream_t* tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(istream && ostream);	

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 0;
		tstream->oowner 	= 0;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;

	} while (0);

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_ag(tb_astream_t* istream, tb_gstream_t* ostream, tb_hize_t offset)
{
	// done
	tb_tstream_t* tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(istream && ostream);	

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 0;
		tstream->oowner 	= 0;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;

	} while (0);

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_au(tb_astream_t* istream, tb_char_t const* ourl, tb_hize_t offset)
{
	// done
	tb_astream_t* 	ostream = tb_null;
	tb_tstream_t* 	tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(istream && ourl);

		// init ostream
		ostream = tb_astream_init_from_url(tb_astream_aicp(istream), ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 0;
		tstream->oowner 	= 1;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;
		
		// ok
		ostream = tb_null;

	} while (0);

	// exit ostream
	if (ostream) tb_astream_exit(ostream, tb_false);
	ostream = tb_null;

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_uu(tb_aicp_t* aicp, tb_char_t const* iurl, tb_char_t const* ourl, tb_hize_t offset)
{
	// done
	tb_astream_t* 	istream = tb_null;
	tb_astream_t* 	ostream = tb_null;
	tb_tstream_t* 	tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(aicp && iurl && ourl);

		// init istream
		istream = tb_astream_init_from_url(aicp, iurl);
		tb_assert_and_check_break(istream);

		// init ostream
		ostream = tb_astream_init_from_url(aicp, ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 1;
		tstream->oowner 	= 1;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;
			
		// ok
		istream = tb_null;
		ostream = tb_null;

	} while (0);

	// exit istream
	if (istream) tb_astream_exit(istream, tb_false);
	istream = tb_null;

	// exit ostream
	if (ostream) tb_astream_exit(ostream, tb_false);
	ostream = tb_null;

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_ua(tb_char_t const* iurl, tb_astream_t* ostream, tb_hize_t offset)
{
	// done
	tb_astream_t* 	istream = tb_null;
	tb_tstream_t* 	tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(iurl && ostream);

		// init istream
		istream = tb_astream_init_from_url(tb_astream_aicp(ostream), iurl);
		tb_assert_and_check_break(istream);

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 1;
		tstream->oowner 	= 0;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;
			
		// ok
		istream = tb_null;

	} while (0);

	// exit istream
	if (istream) tb_astream_exit(istream, tb_false);
	istream = tb_null;

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_du(tb_aicp_t* aicp, tb_byte_t const* idata, tb_size_t isize, tb_char_t const* ourl, tb_hize_t offset)
{
	// done
	tb_astream_t* 	istream = tb_null;
	tb_astream_t* 	ostream = tb_null;
	tb_tstream_t* 	tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(aicp && idata && isize && ourl);

		// init istream
		istream = tb_astream_init_from_data(aicp, idata, isize);
		tb_assert_and_check_break(istream);

		// init ostream
		ostream = tb_astream_init_from_url(aicp, ourl);
		tb_assert_and_check_break(ostream);

		// ctrl file
		if (tb_stream_type(ostream) == TB_STREAM_TYPE_FILE) 
		{
			// ctrl mode
			if (!tb_stream_ctrl(ostream, TB_STREAM_CTRL_FILE_SET_MODE, TB_FILE_MODE_RW | TB_FILE_MODE_CREAT | TB_FILE_MODE_BINARY | TB_FILE_MODE_TRUNC)) break;
		}

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 1;
		tstream->oowner 	= 1;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;
	
		// ok
		istream = tb_null;
		ostream = tb_null;

	} while (0);

	// exit istream
	if (istream) tb_astream_exit(istream, tb_false);
	istream = tb_null;

	// exit ostream
	if (ostream) tb_astream_exit(ostream, tb_false);
	ostream = tb_null;

	// ok?
	return (tb_handle_t)tstream;
}
tb_handle_t tb_tstream_init_da(tb_byte_t const* idata, tb_size_t isize, tb_astream_t* ostream, tb_hize_t offset)
{
	// done
	tb_astream_t* 	istream = tb_null;
	tb_tstream_t* 	tstream = tb_null;
	do
	{
		// check
		tb_assert_and_check_break(idata && isize && ostream);

		// init istream
		istream = tb_astream_init_from_data(tb_astream_aicp(ostream), idata, isize);
		tb_assert_and_check_break(istream);

		// make tstream
		tstream = tb_malloc0(sizeof(tb_tstream_t));
		tb_assert_and_check_break(tstream);

		// init tstream
		tstream->istream 	= istream;
		tstream->ostream 	= ostream;
		tstream->iowner 	= 1;
		tstream->oowner 	= 0;
		tstream->stoped 	= 1;
		tstream->offset 	= offset;
			
		// ok
		istream = tb_null;

	} while (0);

	// exit istream
	if (istream) tb_astream_exit(istream, tb_false);
	istream = tb_null;

	// ok?
	return (tb_handle_t)tstream;
}
tb_bool_t tb_tstream_open(tb_handle_t handle, tb_tstream_open_func_t func, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return_val(tstream && func, tb_false);

	// done
	tb_bool_t ok = tb_false;
	do
	{
		// check
		tb_assert_and_check_break(tb_atomic_get(&tstream->stoped));
		tb_assert_and_check_break(!tb_atomic_get(&tstream->opened));

		// check
		tb_assert_and_check_break(tstream->istream && tb_stream_mode(tstream->istream) == TB_STREAM_MODE_AICO);
		tb_assert_and_check_break(tstream->ostream);

		// clear state
		tb_atomic_set0(&tstream->stoped);
		tb_atomic_set0(&tstream->paused);

		// init some rate info
		tstream->base 	= tb_aicp_time(tb_astream_aicp(tstream->istream));
		tstream->base1s = tstream->base;
		tstream->save 	= 0;
		tstream->save1s = 0;
		tstream->crate 	= 0;

		// init func
 		tstream->func.open.func = func;
		tstream->func.open.priv = priv;

		// open ostream
		tb_bool_t bopened = tb_false;
		if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO)
		{
			if (!tb_stream_is_opened(tstream->ostream))
			{
				if (!tb_astream_open(tstream->ostream, tb_tstream_ostream_open_func, tstream)) break;
			}
			else bopened = tb_true;
		}
		else if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AIOO)
		{
			if (!tb_stream_is_opened(tstream->ostream) && !tb_gstream_open(tstream->ostream)) break;
			bopened = tb_true;
		}
		else tb_assert_and_check_break(0);

		// open and seek istream
		if (bopened && !tb_astream_oseek(tstream->istream, tstream->offset, tb_tstream_istream_open_func, tstream)) break;

		// ok
		ok = tb_true;

	} while (0);

	// failed? stoped
	if (!ok) tb_atomic_set(&tstream->stoped, 1);

	// ok?
	return ok;
}
tb_bool_t tb_tstream_save(tb_handle_t handle, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return_val(tstream && tstream->istream && tstream->ostream && func, tb_false);

	// check state
	tb_assert_and_check_return_val(!tb_atomic_get(&tstream->stoped), tb_false);
	tb_assert_and_check_return_val(tb_atomic_get(&tstream->opened), tb_false);
	
	// save func
	tstream->func.save.func = func;
	tstream->func.save.priv = priv;

	// read it
	return tb_astream_read(tstream->istream, (tb_size_t)tb_atomic_get(&tstream->lrate), tb_tstream_istream_read_func, tstream);
}
tb_bool_t tb_tstream_osave(tb_handle_t handle, tb_tstream_save_func_t func, tb_pointer_t priv)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return_val(tstream && func, tb_false);

	// no opened? open it first
	if (!tb_atomic_get(&tstream->opened))
	{
		tstream->osave.func 	= func;
		tstream->osave.priv 	= priv;
		tstream->osave.tstream 	= tstream;
		return tb_tstream_open(tstream, tb_tstream_open_func, &tstream->osave);
	}

	// save it
	return tb_tstream_save(tstream, func, priv);
}
tb_void_t tb_tstream_kill(tb_handle_t handle)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return(tstream);

	// stop it
	if (!tb_atomic_fetch_and_set(&tstream->stoped, 1))
	{
		// trace
		tb_trace_impl("kill: ..");

		// kill istream
		if (tstream->istream && tb_stream_mode(tstream->istream) == TB_STREAM_MODE_AICO) 
			tb_astream_kill(tstream->istream);

		// kill ostream
		if (tstream->ostream && tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO) 
			tb_astream_kill(tstream->ostream);
	}
}
tb_void_t tb_tstream_clos(tb_handle_t handle, tb_bool_t bcalling)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return(tstream);

	// trace
	tb_trace_impl("clos: ..");

	// kill it first 
	tb_tstream_kill(tstream);

	// close istream
	if (tstream->istream) tb_astream_clos(tstream->istream, bcalling);

	// close ostream
	if (tstream->ostream)
	{
		if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO) 
			tb_astream_clos(tstream->ostream, bcalling);
		else if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AIOO)
			tb_gstream_clos(tstream->ostream);
	}

	// trace
	tb_trace_impl("clos: ok");
}
tb_void_t tb_tstream_exit(tb_handle_t handle, tb_bool_t bcalling)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return(tstream);

	// trace
	tb_trace_impl("exit: ..");

	// close it first 
	tb_tstream_clos(tstream, bcalling);

	// exit istream
	if (tstream->istream && tstream->iowner) tb_astream_exit(tstream->istream, bcalling);
	tstream->istream = tb_null;

	// exit ostream
	if (tstream->ostream && tstream->oowner)
	{
		if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AICO) 
			tb_astream_exit(tstream->ostream, bcalling);
		else if (tb_stream_mode(tstream->ostream) == TB_STREAM_MODE_AIOO)
			tb_gstream_exit(tstream->ostream);
	}
	tstream->ostream = tb_null;

	// exit tstream
	tb_free(tstream);

	// trace
	tb_trace_impl("exit: ok");
}
tb_void_t tb_tstream_pause(tb_handle_t handle)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return(tstream);

	// have been paused?
	tb_check_return(!tb_atomic_get(&tstream->paused));

	// pause it
	tb_atomic_set(&tstream->pausing, 1);
}
tb_bool_t tb_tstream_resume(tb_handle_t handle)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return_val(tstream && tstream->istream && tstream->ostream, tb_false);

	// done
	tb_bool_t ok = tb_false;
	do
	{
		// stoped?
		tb_assert_and_check_break(!tb_atomic_get(&tstream->stoped));

		// not opened? failed
		tb_check_break(tb_atomic_get(&tstream->opened));

		// pausing? failed
		tb_check_break(!tb_atomic_get(&tstream->pausing));

		// not paused? resume ok
		tb_check_return_val(tb_atomic_fetch_and_set0(&tstream->paused), tb_true);

		// init some rate info
		tstream->base 	= tb_aicp_time(tb_astream_aicp(tstream->istream));
		tstream->base1s = tstream->base;
		tstream->save1s = 0;
		tstream->crate 	= 0;

		// read it
		if (!tb_astream_read(tstream->istream, (tb_size_t)tb_atomic_get(&tstream->lrate), tb_tstream_istream_read_func, tstream)) 
		{
			// continue to be paused
			tb_atomic_set(&tstream->paused, 1);
			break;
		}

		// ok
		ok = tb_true;

	} while (0);

	// ok?
	return ok;
}
tb_void_t tb_tstream_limit(tb_handle_t handle, tb_size_t rate)
{
	// check
	tb_tstream_t* tstream = (tb_tstream_t*)handle;
	tb_assert_and_check_return(tstream);

	// set the limit rate
	tb_atomic_set(&tstream->lrate, rate);
}


