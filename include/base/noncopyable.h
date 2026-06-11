#pragma once

/*
noncopyable is a base class which can be inherited to make the derived class non-copyable.
*/

class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    void operator=(const noncopyable&) = delete;
protected:
    noncopyable() = default;
    ~noncopyable() = default;   
};