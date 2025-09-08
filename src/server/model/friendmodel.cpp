#include "friendmodel.hpp"
#include "db.h"


// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 1、组装 sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into friend values(%d, %d)",
            userid, friendid);

    // 2、连接数据库
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}
// 返回用户好友列表  => user与frined表进行联表查询后返回
vector<User> FriendModel::query(int userid)
{
    // 1、组装 sql语句
    char sql[1024] = {0};
    sprintf(sql, "select a.id ,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid=%d", userid);

    vector<User> vec;
    // 2、连接数据库
    MySQL mysql;
    if (mysql.connect())
    {
        // 3、执行 sql语句
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            
            MYSQL_ROW row ;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);

                vec.push_back(user);
            }
            mysql_free_result(res);
        }
    }
    // 返回一个无效的用户对象，
    return vec;
}
