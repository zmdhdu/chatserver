#ifndef USER_H
#define USER_H
#include <string>
using namespace std;
// 将 数据模块 User 表 与业务模块进行解耦
// 针对于 User 表的映射 到 类中呈现，避免直接操作表
// User 表的 ORM类
class User
{
public:
    // 第一种：列表初始化
    // User(int id =-1, string name = "", string password = "", string state = "offline")
    //     : id(id), name(name), password(password), state(state) {}

    // 第二种：赋值初始化
    User(int id = -1, string name = "", string password = "", string state = "offline")
    {
        this->id = id;
        this->name = name;
        this->password = password;
        this->state = state;
    }
    // 对 User 表进行 Set 操作
    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPassword(string password) { this->password = password; }
    void setState(string state) { this->state = state; }
    // 对 User 表进行 Get 操作
    int getId() const { return this->id; }
    string getName() const { return this->name; }
    string getPassword() const { return this->password; }
    string getState() const { return this->state; }

private:
    int id;
    string name;
    string password;
    string state;
};

#endif