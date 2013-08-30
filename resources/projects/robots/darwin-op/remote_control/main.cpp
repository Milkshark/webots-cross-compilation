#include "remote.hpp"
#include "/darwin/Linux/build/streamer/jpeg_utils.h"
#include "Image.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)

typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;

#define PORT 5023

using namespace webots;

void writeINT2Buffer(char * buffer, int value);
int readINTFromBuffer(char * buffer);

int main(int argc, char *argv[]) {

  // we need to set stdout and stderr non-buffered
  // so that messages are actually displayed in the
  // DARwIn-OP console of the robot window
  setvbuf(stdout,NULL,_IONBF,0);
  setvbuf(stderr,NULL,_IONBF,0);

  int cameraWidthZoomFactor = 1;
  int cameraHeightZoomFactor = 1;

  if (argc >= 3) {
    sscanf(argv[1], "%d", &cameraWidthZoomFactor);
    sscanf(argv[2], "%d", &cameraHeightZoomFactor);
  }

  // Server socket
  SOCKADDR_IN sin;
  SOCKET sock;
  socklen_t recsize = sizeof(sin);
    
  // Client socket
  SOCKADDR_IN csin;
  SOCKET csock = NULL;
  socklen_t crecsize;
    
  int sock_err;

  // Creation of the socket
  sock = socket(AF_INET, SOCK_STREAM, 0);
  
  // If  socket is valid
  if (sock != INVALID_SOCKET) {    
    // Configuration
    sin.sin_addr.s_addr = htonl(INADDR_ANY);  // Adresse IP automatic
    sin.sin_family = AF_INET;                 // Protocole familial (IP)
    sin.sin_port = htons(PORT);               // Listening of the port
    bzero(&sin.sin_zero, sizeof(sin.sin_zero)); // make sure the zero are correctly initialized

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(int));

    sock_err = bind(sock, (SOCKADDR*) &sin, recsize);
    
    // If socket works
    if(sock_err != SOCKET_ERROR) {
      // Starting port Listening (server mode)
      sock_err = listen(sock, 1); // only one connexion 

      // If socket works
      if(sock_err != SOCKET_ERROR) {
        // Wait until a client connect
        printf("Waiting for client connection on port %d...\n", PORT);
        csock = accept(sock, (SOCKADDR*)&csin, &crecsize);
        printf("Client connected.\n");
      } else
        perror("listen");
    }
    else
      perror("bind");
    
    Remote * remote = new Remote();
    remote->remoteStep();
    const double *acc;
    const double *gyro;
    const unsigned char *image;

    char *receiveBuffer = (char *)malloc(1024);
    char *sendBuffer = (char *)malloc(350000);
    receiveBuffer[0]='\0';
    sendBuffer[0]='\0';

    Image rgbImage((320/cameraWidthZoomFactor), (240/cameraHeightZoomFactor), Image::RGB_PIXEL_SIZE);
    unsigned char jpeg_buffer[rgbImage.m_ImageSize];
    
    int c = 0;
    int n;
    while(1) {
      // Wait for message
      n=0;
      do
        n += recv(csock, &receiveBuffer[n], 1024-n, 0);
      while(n<3);
      if (receiveBuffer[0]!='W') {
        printf("Error: wrong TCP message received\n");
        continue;
      }
      int total = (unsigned char)receiveBuffer[1]+(unsigned char)receiveBuffer[2]*256;
      while(n<total)
        n += recv(csock, &receiveBuffer[n], 1024-n, 0);

      receiveBuffer[total]=0; // set the final 0

      int receivePos = 3, sendPos = 5;

      // Accelerometer
      if (receiveBuffer[receivePos] == 'A') {
        acc = remote->getRemoteAccelerometer();
        for(c = 0; c<3; c++) 
          writeINT2Buffer(sendBuffer + 4 * c + sendPos, (int)acc[c]);
        sendPos += 12;
        receivePos++;
      }

      // Gyro
      if (receiveBuffer[receivePos] == 'G') {
        gyro = remote->getRemoteGyro();
        for(c = 0; c<3; c++)
          writeINT2Buffer(sendBuffer + 4 * c + sendPos, (int)gyro[c]);
        sendPos += 12;
        receivePos++;
      }

      // Camera
      if (receiveBuffer[receivePos] == 'C') {
        image = remote->getRemoteImage();
        int image_buffer_position = 0;

        for (int height = 120 - (120 / cameraHeightZoomFactor) ; height < 120 + (120 / cameraHeightZoomFactor); height++) {
          for (int width = 160 - (160 / cameraWidthZoomFactor) ; width < 160 + (160 / cameraWidthZoomFactor); width++) {
            rgbImage.m_ImageData[image_buffer_position  + 2] = image[height * 320 * 4 + width * 4 + 0];
            rgbImage.m_ImageData[image_buffer_position  + 1] = image[height * 320 * 4 + width * 4 + 1];
            rgbImage.m_ImageData[image_buffer_position  + 0] = image[height * 320 * 4 + width * 4 + 2];
            image_buffer_position += 3;
          }
        }
        
        // Compress image to jpeg
        int buffer_length = 0;
        if (cameraHeightZoomFactor * cameraWidthZoomFactor < 2) // -> resolution 320x240 -> put quality at 65%
          buffer_length = jpeg_utils::compress_rgb_to_jpeg(&rgbImage, jpeg_buffer, rgbImage.m_ImageSize, 65);
        else // image smaller, put quality at 80%
          buffer_length = jpeg_utils::compress_rgb_to_jpeg(&rgbImage, jpeg_buffer, rgbImage.m_ImageSize, 80);

        writeINT2Buffer(sendBuffer + sendPos, buffer_length); // write image_buffer length 
        sendPos += 4;

        memcpy(sendBuffer + sendPos, jpeg_buffer, buffer_length); // write image
        sendPos += buffer_length;

        receivePos++;
      }

      // LEDs
      for (c = 0; c < 5; c++) {
        if (receiveBuffer[receivePos] == 'L') {
          unsigned char c1 = static_cast <unsigned char> (receiveBuffer[receivePos+4]);
          unsigned char c2 = static_cast <unsigned char> (receiveBuffer[receivePos+3]);
          unsigned char c3 = static_cast <unsigned char> (receiveBuffer[receivePos+2]);
          int value = c1 + (c2 << 8) + (c3 << 16);
          remote->setRemoteLED(receiveBuffer[receivePos+1], value);
          receivePos += 5;
        }
      }

      // Motors Actuator
      for (c = 0; c < 20; c++) {
        if (receiveBuffer[receivePos] == 'S') {
          int motorNumber = (int)receiveBuffer[receivePos+1];
          receivePos+= 2;
          if (receiveBuffer[receivePos] == 'p') { // Position
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorPosition(motorNumber, value);
            receivePos += 5;
          }
          if (receiveBuffer[receivePos] == 'v') { // Velocity
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorVelocity(motorNumber, value);
            receivePos += 5;
          }
          if (receiveBuffer[receivePos] == 'a') { // Acceleration
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorAcceleration(motorNumber, value);
            receivePos += 5;
          }
          if (receiveBuffer[receivePos] == 'm') { // AvailableTorque
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorAvailableTorque(motorNumber, value);
            receivePos += 5;
          }
          if (receiveBuffer[receivePos] == 'c') { // ControlP
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorControlP(motorNumber, value);
            receivePos += 5;
          }
          if (receiveBuffer[receivePos] == 'f') { // Torque
            int value = readINTFromBuffer(receiveBuffer + receivePos + 1);
            remote->setRemoteMotorTorque(motorNumber, value);
            receivePos += 5;
          }
        }
      }
      
      // Motors sensors Position
      for (c = 0; c < 20; c++) {
        if (receiveBuffer[receivePos] == 'P') {
          if ((int)receiveBuffer[receivePos+1] < 20) {
            double motorPosition = remote->getRemoteMotorPosition((int)receiveBuffer[receivePos+1]);
            writeINT2Buffer(sendBuffer + sendPos, (int)motorPosition);
          }
          else
            writeINT2Buffer(sendBuffer + sendPos, 0);
          sendPos += 4;
          receivePos += 2;
        }
      }

      // Motors sensors torque
      for (c = 0; c < 20; c++) {
        if (receiveBuffer[receivePos] == 'F') {
          if ((int)receiveBuffer[receivePos+1] < 20) {
            double motorTorque = remote->getRemoteMotorTorque((int)receiveBuffer[receivePos+1]);
            writeINT2Buffer(sendBuffer + sendPos, (int)motorTorque);
          } else
            writeINT2Buffer(sendBuffer + sendPos, 0);
          sendPos += 4;
          receivePos += 2;
        }
      }
      if (receiveBuffer[receivePos]!=0)
        printf("Error: received unknown message: %c\n",receiveBuffer[receivePos]);

      // Terminate the buffer and send it
      sendBuffer[0] = 'W';
      writeINT2Buffer(sendBuffer+1, sendPos); // Write size of buffer at the beginning
      sendBuffer[sendPos++] = '\0';
      n=0;
      do
        n += send(csock, &sendBuffer[n], sendPos-n, 0);
      while (n!=sendPos);
      remote->remoteStep();
    }

    // Close client socket and server socket
    printf("Closing client socket\n");
    closesocket(csock);
    printf("Closing server socket\n");
    closesocket(sock);
    free(receiveBuffer);
    free(sendBuffer);
  }
  else
    perror("socket");
  return EXIT_SUCCESS;
}

void writeINT2Buffer(char * buffer, int value) {
  buffer[0] = value >> 24;
  buffer[1] = (value >> 16) & 0xFF;
  buffer[2] = (value >> 8) & 0xFF;
  buffer[3] = value & 0xFF;
}

int readINTFromBuffer(char * buffer) {
  unsigned char c1 = static_cast <unsigned char> (buffer[3]);
  unsigned char c2 = static_cast <unsigned char> (buffer[2]);
  unsigned char c3 = static_cast <unsigned char> (buffer[1]);
  unsigned char c4 = static_cast <unsigned char> (buffer[0]);
  return (c1 + (c2 << 8) + (c3 << 16) + (c4 << 24));
}
