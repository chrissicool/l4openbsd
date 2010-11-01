/*
 * dispatcher
 *
 * This implements the vCPU entry IP. It will be executed, when the vCPU gets
 * interrupted, for SIGILL, SIGSEGV, SIGFPE and so on. We need to generate and
 * deliver the signals to the offending process.
 */

#include <machine/l4/vcpu.h>

void
l4x_vcpu_entry(void)
{

}
