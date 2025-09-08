#ifndef GROUP_H
#define GROUP_H

#include "groupuser.hpp"
#include <string>
#include <vector>
using namespace std;

// User表的 ORM类  ： 通过面向对象的方式操作数据库，而不需要直接编写 SQL 语句

class Group
{
public:
// 初始化
    Group(int id=-1,string name="",string desc = "")
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
    }

    // 对 groupUser 表进行 Set 操作
    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setDesc(string desc) { this->desc = desc; }
    // 对 groupUser 表进行 Get 操作
    int getId() const { return this->id; }
    string getName() const { return this->name; }
    string getDesc() const { return this->desc; }
    vector<GroupUser> &getUsers() { return this->users; }

private:
    int id;
    string name;
    string desc;
    vector<GroupUser> users;
};

#endif