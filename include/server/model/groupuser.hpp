#ifndef GROUPUSER_H
#define GROUPUSER_H

#include "user.hpp"

// 表示群组用户，多了一个role 角色信息，从 User类直接继承，复用 User其他信息
class GroupUser:public User
{
public:
    void setRole(string role)
    {
        this->role = role;
    }
    string getRole()
    {
        return this->role;
    }

private:
    string role;
};

#endif