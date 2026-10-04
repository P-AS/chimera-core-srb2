/* i_net.c - what of upstream's netcode/i_tcp.c the engine names, in a
 * machine with no network (upstream's dummy/i_net.c answers I_InitNetwork):
 * a game is always local, its one node the machine's own. */
#include "doomdef.h"
#include "netcode/d_net.h"
#include "netcode/i_tcp.h"

boolean I_InitTcpNetwork(void) { return false; }
boolean Net_IsNodeIPv6(INT32 node)
{
	(void)node;
	return false;
}
