# test with smaller relay send buffer delay before server

use strict;
use warnings;

our %args = (
    client => {
	len => 2**17,
    },
    relay => {
	sndbuf => 2**12,
    },
    server => {
	func => sub { sleep 3; read_char(@_); },
    },
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);

1;