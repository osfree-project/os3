#ifndef __CPI_SESSION_CONNECTION_H__
#define __CPI_SESSION_CONNECTION_H__

#include <cpi_session/client.h>
#include <base/connection.h>

namespace OS2::Cpi { struct Connection; }

struct OS2::Cpi::Connection : Genode::Connection<Session>, Session_client
{
	Connection(Genode::Env &env, Label const &label   = Label())
	:
		/* create session */
		Genode::Connection<OS2::Cpi::Session>(env, label, Ram_quota { 6 * 1024 }, Args()),
		/* initialize RPC interface */
		Session_client(cap()) { }
};

#endif
