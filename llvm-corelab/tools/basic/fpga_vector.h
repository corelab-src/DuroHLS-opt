#ifndef FPGA_VECTOR_H
#define FPGA_VECTOR_H

#include <stdio.h>

/*** Type T1 & T2 should be primitive types such as int, char, ... ***/
/*** T1 & T2 must support '==' operation ***/
template <typename T>
class FPGAVector{
  public:
    FPGAVector<T>(){
      elementNum = 0;
    }

    bool push_back(T value){
      if(elementNum == 64)
        return false;
      elements[elementNum++] = value;
      return true;
    }

    bool erase(int index){
      if(index > (elementNum-1) || index < 0)
        return false;
      
      if(index == (elementNum-1))
        elementNum--;
      else {
        for(int i=index;i<elementNum-1;i++)
          elements[i] = elements[i+1];
        elementNum--;
      }
      return true;
    }

    T getElem(int index){
      return elements[index]; 
    }

    bool isExist(T value){
      for(int i=0;i<elementNum;i++)
        if(elements[i] == value)
          return true;
      return false;
    }

    int getSize(){
      return elementNum;
    }
  private:
    T elements[64];
    int elementNum;
};

#endif
