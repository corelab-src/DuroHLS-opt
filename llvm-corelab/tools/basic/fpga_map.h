#ifndef FPGA_MAP_H
#define FPGA_MAP_H

#include <stdio.h>

/*** Type T1 & T2 should be primitive types such as int, char, ... ***/
/*** T1 & T2 must support '==' operation ***/
template <typename T1, typename T2>
class FPGAMap{
  public:
    FPGAMap<T1, T2>(){
      elementNum = 0;
    }

    bool insert(T1 key, T2 value){
      if(elementNum == 64)
        return false;
      else if(elementNum == 0){
        elements[elementNum].key = key;
        elements[elementNum].value = value;
        elementNum++;
        printf("elementNum : %d\n",elementNum);
        return true;
      }
      for(int i=0;i<elementNum;i++){
        if(elements[i].key == key){
          elements[i].value = value;
          printf("element is already exist\n");
          return true;
        }          
      }
      FPGAMapElem newElem;
      newElem.key = key;
      newElem.value = value;
      elements[elementNum] = newElem;
      elementNum++;
      printf("elementNum : %d\n",elementNum);
      return true;
    }

    T2* getElem(T1 key){
      for(int i=0;i<elementNum;i++){
        if(elements[i].key == key)
          return &(elements[i].value);
      }
      return NULL;
    }

    T2* getElemByIndex(int index){
      return &(elements[index].value);
    }

    bool erase(T1 key){
      if(elementNum == 0)
        return false;
      for(int i=0;i<elementNum;i++){
        if(elements[i].key == key){
          elements[i].key = elements[elementNum-1].key;
          elements[i].value = elements[elementNum-1].value;
          elementNum--;
          return true;
        }
      }
      return false;
    }

    bool isExist(T1 key){
      if(elementNum == 0)
        return false;
      for(int i=0;i<elementNum;i++){
        if(elements[i].key == key)
          return true;
      }
      return false;
    }

    int getSize(){
      return elementNum;
    }
  private:
    typedef struct FPGAMapElem{
      T1 key;
      T2 value;
    }FPGAMapElem;

    FPGAMapElem elements[64];
    int elementNum;
};

#endif
