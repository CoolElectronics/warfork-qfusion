#include <cstdint>
#include <stddef.h>
#include <cassert>
#include <cstring>
#include "os.h"

PipeType GPipeRead = NULLPIPE;
PipeType GPipeWrite = NULLPIPE;

#define MESSAGE_MAX 1024

class pipebuff_t
{
  public:
  char buffer[MESSAGE_MAX];
  unsigned int cursize = 0;

  unsigned int br = 0;
  bool hasmsg = false;
  

  // cursize, data, whatever is in pipebuff_t now
  pipebuff_t()
  {
    // init all crap
  }
  ~pipebuff_t()
  {
    // destroy all crap
  }

  void WriteData(void* val, size_t vallen)
  {
    assert(cursize + vallen < MESSAGE_MAX);
    memcpy(buffer + cursize, val, vallen);
    cursize += vallen;
  }

  void WriteByte(char val)
  {
    WriteData(&val, sizeof val);
  }

  void WriteInt(int val)
  {
    WriteData(&val, sizeof val);
  }

  void WriteFloat(float val)
  {
    WriteData(&val, sizeof val);
  }

  void WriteLong(long long val){
    WriteData(&val, sizeof val);
  }

  void *ReadData(size_t vallen)
  {
    assert(cursize + vallen < MESSAGE_MAX);
    void* val = cursize + buffer;
    cursize += vallen;
    return val;
  }

  char *ReadString()
  {
    char *str = cursize + buffer;
    unsigned int len = strlen(str);
    cursize +=len;

    return str;
  }

  char ReadByte(){
    return *(char*)ReadData(sizeof(char));
  }

  int ReadInt(){
    return *(int*)ReadData(sizeof(int));
  }

  int ReadFloat(){
    return *(float*)ReadData(sizeof(float));
  }

  int ReadLong(){
    return *(long long*)ReadData(sizeof(long long));
  }




  int Transmit()
  {
    printf("writing pipe %i\n", cursize);
    writePipe(GPipeWrite, &cursize, sizeof cursize);
    return writePipe(GPipeWrite, buffer, cursize);
  }

  int Recieve()
  {
    // reset after a succesful message read. could be more explicit but idc
    if (hasmsg)
    {
      cursize = 0;
      hasmsg = false;
      memset(buffer,0,sizeof buffer);
    }


    // read message length header
    int msglen = (br > 0) ? (*(uint32_t*) &buffer[0]) : 0;

    if (br < (msglen + sizeof(uint32_t)))  /* we have an incomplete commmand. Try to read more. */
    {
        if (pipeReady(GPipeRead))
        {

            assert(br < sizeof(buffer));
            const int morebr = readPipe(GPipeRead, buffer + br, sizeof (buffer) - br);
            if (morebr > 0)
                br += morebr;
            else  /* uh oh */
            {
              return 0;
            }
        }
    }


    // we have a full command
    if (msglen && (br >= msglen + sizeof(uint32_t)))
    {
      hasmsg = true;

      br -= msglen + sizeof(uint32_t);
      if (br > 0){
        printf("OVERRINNING!!!\n");
        // we have extra data left over, shift it to the left
        memmove(buffer, buffer+msglen+sizeof(uint32_t), br);
      }

    }

    return 1;
  }
};


int Write1ByteMessage(const uint8_t message){
  pipebuff_t buf;
  buf.WriteByte(message);
  return buf.Transmit();
}

//
// //???
int what(){
  pipebuff_t wtf;
  wtf.Recieve();
  wtf.ReadByte();
  wtf.ReadInt();
  wtf.WriteLong(0);
  wtf.ReadString();
  wtf.ReadLong();
}
