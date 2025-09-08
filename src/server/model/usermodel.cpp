#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;

// 对 user 表 增
bool UserModel::insert(User &user)
{
    // 1、组装 sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into user(name,password,state) values('%s','%s','%s')",
            user.getName().c_str(), user.getPassword().c_str(), user.getState().c_str());

    // 2、连接数据库
    MySQL mysql;
    if(mysql.connect())
    {
        // 3、执行 sql语句
        if(mysql.update(sql))
        {
            // 获取成功插入成功的用户数据生成的 主键 id
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
        
    }
    return false;
}

// 对 user 表 查
User UserModel::query(int id)
{
    // 1、组装 sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    // 2、连接数据库
    MySQL mysql;
    if(mysql.connect())
    {
        // 3、执行 sql语句
        MYSQL_RES *res = mysql.query(sql);
        if(res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if(row != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPassword(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                return user;
            }
            mysql_free_result(res);
        }
    }
    // 返回一个无效的用户对象，ID为-1表示用户不存在
    return User(-1, "", "", "");
}
// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1、组装 sql语句
    char sql[1024] = {0};
    // / 修正SQL语法：使用UPDATE... SET... WHERE结构，
    sprintf(sql, "UPDATE user SET state='%s' WHERE id=%d",
            user.getState().c_str(), user.getId());

    // 2、连接数据库
    MySQL mysql;
    if (mysql.connect())
    {
        // 3、执行 sql语句
        if (mysql.update(sql))
        {
            // // 获取成功插入成功的用户数据生成的 主键 id
            // user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1、组装 sql语句
    char sql[1024] = "update  user set state = 'offline' where state ='online'";
    
    // 2、连接数据库
    MySQL mysql;
    if (mysql.connect())
    {
        // 3、执行 sql语句
        mysql.update(sql);
    }
   
}