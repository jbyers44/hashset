#ifndef COMMON_H
#define COMMON_H

template<typename T> class set {

    public:

        virtual ~set() {}
        
        virtual bool add(T value)       = 0;

        virtual bool remove(T value)    = 0;

        virtual bool contains(T value)  = 0;

        virtual int size()              = 0;

        virtual void populate(int size, T (*random_T)()) = 0;
};

#endif