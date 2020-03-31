// C lib Header
#include <stdio.h>
#include <stdlib.h>
//linux header
#include <string.h>
#include <unistd.h> 	// write(), read(), close()
#include <fcntl.h>		// Contains file controls like O_RDWR
#include <termios.h> 	// Contains POSIX terminal control definitions
#include <errno.h>		// Error integer and strerror() function
#include <sys/ioctl.h>

/*GLOBAL*/
#define BAUDRATE B115200

const char *PORTNAME = "/dev/serial0";
char *msgbuf = "FUCKSHIT";
char buf_read[255];//2^8 = 255

/*tty parameter*/
struct termios options;
int tty_fd;
int ret;




// sending data-tx
int sending_data(int fd,const char *str,size_t len)
{
	if (write(fd, str,len) < 0){
		return errno;
	}

	//  sleep enough to transmit the length plus receive 25:
    //  approx 100 uS per char transmit
	usleep((len * 100));
	return 0;
  	// usleep(500*1000);
}
// receivinf data-rx

int reading_data(int fd){
	size_t len =  sizeof(buf_read)/sizeof(buf_read[0]);
	int byte_read = read(fd,&buf_read[0], len);

	if (byte_read < 0){
		return errno;
	}
	usleep((len * 100));
	return byte_read;
}

/*Add the serial port handle in the future*/
void serial_release_handle(const int fd)
{
	/*close the fd of tty*/
	close(fd);
}



//setting attribute
int SetInterfaceAttribs(int fd)
{
	/*tty parameter*/
	struct termios options;

	//tcgetattr returns 1 if set, 0 if not set < 0 on error
	if (tcgetattr (fd, &options) < 0){
			fprintf (stderr, "error %d from tcgetattr", errno);
			return -1;
	}
	memset (&options, 0, sizeof options);
	cfsetspeed(&options, BAUDRATE);
	cfmakeraw(&options);
	options.c_cflag &= ~CSTOPB;
	options.c_cflag = CS8|CREAD|CLOCAL;// 8n1, see termios.h for more information
	options.c_cc[VTIME]=1;	//
	options.c_cc[VMIN]= 10;	// 1 seconds read timeout
	//	tcsetattr
	if (tcsetattr (fd, TCSANOW, &options) != 0){
			fprintf (stderr, "error=-%d %s from tcsetattr", errno, strerror(errno));
			return -1;
	}
	return fd;
}
int main(int argc, char** argv) {

	/*tty parameter*/
	//struct termios options;
	int tty_fd;
	int ret;

	memset(&buf_read, '\0', sizeof(buf_read));
	/*force to close the service changed with config*/
	system("sudo systemctl stop serial-getty@ttyAMA0.service");

	tty_fd = open(PORTNAME, O_RDWR | O_NOCTTY|O_NONBLOCK );

	if (tty_fd < 0) {
			printf("Failed to open port %s\n: %s\n" ,PORTNAME, strerror(errno));
			goto error_port_handler;
	}
	//setting attribute
	tty_fd = SetInterfaceAttribs(tty_fd);
	if(tty_fd < 0)
		return -1;
	//writing data
	ret = sending_data(tty_fd, msgbuf,strlen(msgbuf));
	if (ret < 0){
		fprintf (stderr, "write failed:errno = -%d %s", errno, strerror(errno));
		goto error_port_handler;
	}
	usleep(10000);
	int bytes;
	/*IOCTL????*/
	ret = ioctl(tty_fd, FIONREAD, &bytes);
	if (ret < 0){
		fprintf (stderr, "IOCTL failed:errno = -%d %s", errno, strerror(errno));
		goto error_port_handler;
	}
	//receving data	add the mechanism to check what of reading not work
	ret = reading_data(tty_fd);
	//printf("the return of the rx is %d\n",ret);
	if (ret < 0){
		fprintf (stderr, "READ failed: errno = -%d %s", errno, strerror(errno));
		goto error_port_handler;
	}
	for(int i = 0; i< ret; i++)
   		printf("%c(%d %#x)\t", buf_read[i], buf_read[i], buf_read[i]);
	printf("\n");
	close(tty_fd);
	return (EXIT_SUCCESS);

error_port_handler:
	serial_release_handle(tty_fd);
	return -1;
}
