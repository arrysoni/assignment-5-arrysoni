#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"


// TAs Himashveta, Ashwin, Nimay, and Mustafa have guided me to debug this, and understand the logic behind this

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
bool nread(int fd, int len, uint8_t *buf)
{

  int total_read = 0; // Total bytes read so far
  int bytes_read = 0; // Bytes read in a read call


  // Loop until the total number of bytes read = requested length 
  while (total_read < len)
  {
    bytes_read = read(fd, buf + total_read, len - total_read);

    // Handle errors occurs while reading
    if (bytes_read < 0)
    {
      // Handle interruption by a signal
      if (errno == EINTR)
      {
        continue;
      }
      else
      {
        // Any other forms of error
        printf("Error in read");
        return false;
      }
    }

    // Reached EOF (end of file)
    else if (bytes_read == 0)
    {
      break;
    }

    total_read += bytes_read;
  }

  // Returns true/false, as per the condition
  return total_read == len;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
bool nwrite(int fd, int len, uint8_t *buf)
{
  int remaining = len; // Bytes left to write
  uint8_t *ptr = buf;  // Pointer to the current position in the buffer

  // Loop until all bytes are written
  while (remaining > 0)
  {
    int bytes_written = write(fd, ptr, remaining);

    if (bytes_written > 0)
    {
      // Updating the variables
      remaining -= bytes_written;     // Decreasing the remaining bytes to be written
      ptr += bytes_written;           // Moving the pointer forward by amount of bytes written
    }

    // Handle errors occurs while reading
    else if (bytes_written < 0)
    {
      // Handle interruption by a signal
      if (errno == EINTR)
      {
        continue;
      }
      else
      {
        // Any other forms of error
        printf("Error in write");
        return false;
      }
    }
  }

  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
bool recv_packet(int fd, uint32_t *op, uint8_t *ret, uint8_t *block)
{

  // Buffer for the packet header: opcode (4 bytes) + info code (1 byte)
  uint8_t header[5];

  // Read the packet header from the file descriptor
  // Reading the header failed
  if (nread(fd, HEADER_LEN, header) == false)
  {
    printf("Failed to read packet header");
    return false;
  }


  // Store the opcode in network byte order
  uint32_t network_op;

  // Memcpy opcode from the header buffer into the network_op 
  memcpy(&network_op, header, sizeof(network_op));
  
  // Opcode converted from network byte to host byte (network to host)
  *op = ntohl(network_op);

  // Extract the info code from the 5th byte of the header
  *ret = header[4];

  // If second lowest bit of info code indicates a block
  if ((*ret & 0x02) && block != NULL)
  {
    // Data block present, read it from the file descriptor into the block buffer
    if (nread(fd, JBOD_BLOCK_SIZE, block) == false)
    {
      printf("Failed to read data block.");
      return false;
    }
  }

  return true;
}


/* attempts to send a packet to sd; returns true on success and false on
 * failure */
bool send_packet(int fd, uint32_t op, uint8_t *block)
{
  // Buffer for the packet header: opcode (4 bytes) + info code (1 byte)
  uint8_t header[5]; 

  // Convert the opcode op from host byte order to network byte order
  uint32_t network_op = htonl(op);

  // Copy the network-order opcode into the header buffer
  memcpy(header, &network_op, sizeof(network_op));

  // Info code set to the 5th byte of the header
  // If the opcode represents a write operation, set the second lowest bit of the code to 1
  header[4] = ((op >> 12) == JBOD_WRITE_BLOCK) ? 0x02 : 0x00;

  // Header sent to the file descriptor using nwrite
  if (nwrite(fd, HEADER_LEN, header) == false)
  {
    printf("Failed to send packet header.");
    return false;
  }

  // Check if the operation is a write block operation
  if ((op >> 12) == JBOD_WRITE_BLOCK)
  {
    if (nwrite(fd, JBOD_BLOCK_SIZE, block) == false)
    {
      printf("Failed to send data block.");
      return false;
    }
  }

  return true;
}


/* connect to server and set the global client variable to the socket */
bool jbod_connect(const char *ip, uint16_t port)
{

  // Create a socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1)
  {
    printf("Failed to create socket");
    return false;
  }

  // Set up the server address structure to 0
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;

  // Convert the given port number to network byte order
  server_addr.sin_port = htons(JBOD_PORT); 

  // Convert IP address from string to binary
  if (inet_aton(ip, &server_addr.sin_addr) <= 0)
  {
    printf("Invalid IP address");
    close(cli_sd);
    cli_sd = -1;
    return false;
  }

  // Attempt to connect to the JBOD server
  if (connect(cli_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    printf("HERE %d \n", connect(cli_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)));
    printf("Failed to connect to server");
    close(cli_sd);
    cli_sd = -1;
    return false;
  }

  return true;
}

void jbod_disconnect(void)
{
  // Check if a connection exists
  if (cli_sd >= 0)
  {
    // Close the socket
    close(cli_sd);

    // Reset the client socket descriptor
    cli_sd = -1; 
  }
}

int jbod_client_operation(uint32_t op, uint8_t *block)
{
  // To receive the response packet
  uint32_t received_op;
  uint8_t info_code;
  uint8_t buffer[JBOD_BLOCK_SIZE];

  // Check if the connection exists
  if (cli_sd == -1)
  {
    printf("Not connected to the server");
    return -1;
  }

  // Check if the packet was sent
  if (send_packet(cli_sd, op, block) == false)
  {
    printf("Packet couldn't be sent to the server");
    return -1;
  }

  // Check if the packet couldn't be received
  if (recv_packet(cli_sd, &received_op, &info_code, buffer) == false)
  {
    printf("Packet couldn't be received from the server");
    return -1;
  }

  // Validate the response
  if (received_op != op)
  {
    printf("Received opcode does not match the sent opcode.\n");
    return -1;
  }

  // Handle data block (if present)
  if ((info_code & 0x02) && block != NULL)
  {
    memcpy(block, buffer, JBOD_BLOCK_SIZE);
  }

  // Return the result (lowest bit of the info code)
  return (info_code & 0x01) ? -1 : 0;
}
