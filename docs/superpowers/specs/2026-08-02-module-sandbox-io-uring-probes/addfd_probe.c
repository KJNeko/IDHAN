// Can a seccomp supervisor inject an io_uring fd into a supervised process?
// SECCOMP_IOCTL_NOTIF_ADDFD goes through receive_fd(), not scm_fp_copy(), so the
// SCM_RIGHTS ban on io_uring files may not apply on this path.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// The syscall the child traps on to ask for its input. Chosen because nothing else uses it.
#define REQUEST_SYSCALL __NR_userfaultfd

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

static int recv_fd( int sock )
{
	char body;
	struct iovec iov = { .iov_base = &body, .iov_len = 1 };
	char control[ CMSG_SPACE( sizeof( int ) ) ] = { 0 };
	struct msghdr msg = { 0 };
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof( control );
	if ( recvmsg( sock, &msg, 0 ) < 0 ) return -errno;
	struct cmsghdr* c = CMSG_FIRSTHDR( &msg );
	if ( !c ) return -ENOENT;
	int fd;
	memcpy( &fd, CMSG_DATA( c ), sizeof( int ) );
	return fd;
}

int main( void )
{
	int sv[ 2 ];
	socketpair( AF_UNIX, SOCK_STREAM, 0, sv );

	pid_t child = fork();

	if ( child == 0 )
	{
		close( sv[ 0 ] );
		setvbuf( stdout, NULL, _IONBF, 0 );

		// Trap REQUEST_SYSCALL to the supervisor, allow everything else.
		struct sock_filter filter[] = {
			BPF_STMT( BPF_LD | BPF_W | BPF_ABS, offsetof( struct seccomp_data, nr ) ),
			BPF_JUMP( BPF_JMP | BPF_JEQ | BPF_K, REQUEST_SYSCALL, 0, 1 ),
			BPF_STMT( BPF_RET | BPF_K, SECCOMP_RET_USER_NOTIF ),
			BPF_STMT( BPF_RET | BPF_K, SECCOMP_RET_ALLOW ),
		};
		struct sock_fprog prog = { .len = 4, .filter = filter };

		prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 );

		int listener = syscall( __NR_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_NEW_LISTENER, &prog );
		if ( listener < 0 )
		{
			printf( "child: installing the listener failed: %s\n", strerror( errno ) );
			_exit( 1 );
		}

		if ( send_fd( sv[ 1 ], listener ) != 0 )
		{
			printf( "child: could not hand the listener to the supervisor\n" );
			_exit( 1 );
		}

		// Ask for the input. The supervisor answers with a descriptor, not a real userfaultfd.
		long got = syscall( REQUEST_SYSCALL, 0 );

		if ( got < 0 )
		{
			printf( "child: request failed: %s\n", strerror( (int)-got ) );
			_exit( 1 );
		}

		printf( "child: received fd %ld\n", got );

		// Prove it is a usable ring: map it and read the registered file through slot 0.
		struct io_uring ring;
		struct io_uring_params p;
		memset( &p, 0, sizeof( p ) );

		// The supervisor sends its params over the socket, as the real design would.
		if ( read( sv[ 1 ], &p, sizeof( p ) ) != (ssize_t)sizeof( p ) )
		{
			printf( "child: no params\n" );
			_exit( 1 );
		}

		if ( io_uring_queue_mmap( (int)got, &p, &ring ) < 0 )
		{
			printf( "child: queue_mmap failed: %s\n", strerror( errno ) );
			_exit( 1 );
		}

		char buf[ 64 ] = { 0 };
		struct io_uring_sqe* sqe = io_uring_get_sqe( &ring );
		io_uring_prep_read( sqe, 0, buf, sizeof( buf ) - 1, 0 );
		sqe->flags |= IOSQE_FIXED_FILE;
		io_uring_submit( &ring );

		struct io_uring_cqe* cqe;
		io_uring_wait_cqe( &ring, &cqe );

		if ( cqe->res < 0 )
			printf( "child: read through the ring failed: %s\n", strerror( -cqe->res ) );
		else
			printf( "child: read %d bytes through the ring: %.*s", cqe->res, cqe->res, buf );

		_exit( 0 );
	}

	close( sv[ 1 ] );

	int listener = recv_fd( sv[ 0 ] );
	if ( listener < 0 )
	{
		printf( "supervisor: no listener: %s\n", strerror( -listener ) );
		return 1;
	}

	// Build the restricted ring the design wants to hand over.
	int target = open( "/etc/hostname", O_RDONLY );

	struct io_uring_params p;
	memset( &p, 0, sizeof( p ) );
	p.flags = IORING_SETUP_R_DISABLED;

	struct io_uring ring;
	if ( io_uring_queue_init_params( 8, &ring, &p ) < 0 )
	{
		printf( "supervisor: ring setup failed\n" );
		return 1;
	}

	io_uring_register_files( &ring, &target, 1 );

	struct io_uring_restriction res[ 3 ];
	memset( res, 0, sizeof( res ) );
	res[ 0 ].opcode = IORING_RESTRICTION_SQE_OP;
	res[ 0 ].sqe_op = IORING_OP_READ;
	res[ 1 ].opcode = IORING_RESTRICTION_SQE_FLAGS_REQUIRED;
	res[ 1 ].sqe_flags = IOSQE_FIXED_FILE;
	res[ 2 ].opcode = IORING_RESTRICTION_REGISTER_OP;
	res[ 2 ].register_op = IORING_REGISTER_RING_FDS;
	io_uring_register_restrictions( &ring, res, 3 );
	io_uring_enable_rings( &ring );

	// Wait for the child to ask.
	struct seccomp_notif* req = calloc( 1, sizeof( struct seccomp_notif ) );
	struct seccomp_notif_resp* resp = calloc( 1, sizeof( struct seccomp_notif_resp ) );

	if ( ioctl( listener, SECCOMP_IOCTL_NOTIF_RECV, req ) < 0 )
	{
		printf( "supervisor: NOTIF_RECV failed: %s\n", strerror( errno ) );
		return 1;
	}

	// Inject the ring and make it the syscall's return value.
	struct seccomp_notif_addfd addfd;
	memset( &addfd, 0, sizeof( addfd ) );
	addfd.id = req->id;
	addfd.srcfd = (__u32)ring.ring_fd;
	addfd.newfd = 0;
	addfd.flags = SECCOMP_ADDFD_FLAG_SEND;
	addfd.newfd_flags = O_CLOEXEC;

	int injected = ioctl( listener, SECCOMP_IOCTL_NOTIF_ADDFD, &addfd );

	if ( injected < 0 )
	{
		printf( "supervisor: ADDFD of the ring -> %s\n", strerror( errno ) );

		// Answer so the child does not hang, then report the control result.
		resp->id = req->id;
		resp->error = -EPERM;
		resp->val = 0;
		resp->flags = 0;
		ioctl( listener, SECCOMP_IOCTL_NOTIF_SEND, resp );

		wait( NULL );
		return 1;
	}

	printf( "supervisor: ADDFD of the ring -> injected as fd %d\n", injected );

	write( sv[ 0 ], &p, sizeof( p ) );

	wait( NULL );

	return 0;
}
