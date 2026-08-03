// Can an io_uring descriptor cross a unix socket via SCM_RIGHTS?
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static int send_fd( int sock, int fd )
{
	char body = 'x';
	struct iovec iov = { .iov_base = &body, .iov_len = 1 };
	char control[ CMSG_SPACE( sizeof( int ) ) ] = { 0 };

	struct msghdr msg = { 0 };
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof( control );

	struct cmsghdr* c = CMSG_FIRSTHDR( &msg );
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN( sizeof( int ) );
	memcpy( CMSG_DATA( c ), &fd, sizeof( int ) );

	return sendmsg( sock, &msg, MSG_NOSIGNAL ) < 0 ? -errno : 0;
}

int main( void )
{
	int sv[ 2 ];
	socketpair( AF_UNIX, SOCK_STREAM, 0, sv );

	// 1. a plain file, as a control
	int plain = open( "/etc/hostname", O_RDONLY );
	int r = send_fd( sv[ 0 ], plain );
	printf( "plain file fd        -> %s\n", r == 0 ? "sent" : strerror( -r ) );

	// 2. an ordinary io_uring
	struct io_uring ring;
	if ( io_uring_queue_init( 8, &ring, 0 ) < 0 )
	{
		printf( "io_uring_queue_init failed\n" );
		return 1;
	}
	r = send_fd( sv[ 0 ], ring.ring_fd );
	printf( "io_uring ring fd     -> %s\n", r == 0 ? "sent" : strerror( -r ) );

	// 3. a restricted, registered-file ring -- what the design actually ships
	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	p.flags = IORING_SETUP_R_DISABLED;

	struct io_uring restricted;
	if ( io_uring_queue_init_params( 8, &restricted, &p ) < 0 )
	{
		printf( "restricted setup failed\n" );
		return 1;
	}

	io_uring_register_files( &restricted, &plain, 1 );

	struct io_uring_restriction res[ 3 ];
	memset( res, 0, sizeof( res ) );
	res[ 0 ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ 0 ].sqe_op = IORING_OP_READ;
	res[ 1 ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ 1 ].sqe_flags = IOSQE_FIXED_FILE;
	res[ 2 ].opcode = IORING_RESTRICTION_REGISTER_OP;
	res[ 2 ].register_op = IORING_REGISTER_RING_FDS;
	io_uring_register_restrictions( &restricted, res, 3 );
	io_uring_enable_rings( &restricted );

	r = send_fd( sv[ 0 ], restricted.ring_fd );
	printf( "restricted ring fd   -> %s\n", r == 0 ? "sent" : strerror( -r ) );

	// 4. a memfd, the fallback path, as a second control
	int mem = memfd_create( "probe", 0 );
	r = send_fd( sv[ 0 ], mem );
	printf( "memfd fd             -> %s\n", r == 0 ? "sent" : strerror( -r ) );

	return 0;
}
