#include "client.h"

client::client(int port,string ip):server_port(port),server_ip(ip){}
client::~client(){
    close(sock);
}
void client::run(){

    //定义sockfd
    sock = socket(AF_INET,SOCK_STREAM, 0);
    
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);  //服务器端口
    servaddr.sin_addr.s_addr = inet_addr(server_ip.c_str());  //服务器ip

    //连接服务器，成功返回0，错误返回-1
    if (connect(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("connect");
        exit(1);
    }
    cout<<"连接服务器成功\n";

    HandleClient(sock);

    return;
}
void client::HandleClient(int conn){
    int choice;
    string name,pass,pass1;
    bool if_login=false;//记录是否登录成功
    string login_name;//记录成功登录的用户名

    //发送本地cookie，并接收服务器答复，如果答复通过就不用登录
    //先检查是否存在cookie文件
    ifstream f("cookie.txt");
    string cookie_str;
    if(f.good()){
        f>>cookie_str;
        f.close();
        cookie_str="cookie:"+cookie_str;
        //将cookie发送到服务器
        send(sock,cookie_str.c_str(),cookie_str.length()+1,0);
        //接收服务器答复
        char cookie_ans[100];
        memset(cookie_ans,0,sizeof(cookie_ans));
        recv(sock,cookie_ans,sizeof(cookie_ans),0);
        //判断服务器答复是否通过
        string ans_str(cookie_ans);
        if(ans_str!="NULL"){//redis查询到了cookie，通过
            if_login=true;
            login_name=ans_str;
        }
    }    
    if(!if_login){
        cout<<" ------------------\n";
        cout<<"|                  |\n";
        cout<<"| 请输入你要的选项:|\n";
        cout<<"|    0:退出        |\n";
        cout<<"|    1:登录        |\n";
        cout<<"|    2:注册        |\n";
        cout<<"|                  |\n";
        cout<<" ------------------ \n\n";
    }

    //开始处理注册、登录事件
    while(1){
        if(if_login)
           break;
        cin>>choice;
        if(choice==0)
            break;
        //注册
        else if(choice==2){
            cout<<"注册的用户名:";
            cin>>name;
            while(1){
                cout<<"密码:";
                cin>>pass;
                cout<<"确认密码:";
                cin>>pass1;
                if(pass==pass1)
                    break;
                else
                    cout<<"两次密码不一致!\n\n";
            }
            name="name:"+name;
            pass="pass:"+pass;
            string str=name+pass;
            send(conn,str.c_str(),str.length(),0);
            cout<<"注册成功！\n";
            cout<<"\n继续输入你要的选项:";
        }
        //登录
        else if(choice==1&&!if_login){
            while(1){
                cout<<"用户名:";
                cin>>name;
                cout<<"密码:";
                cin>>pass;
                string str="login"+name;
                str+="pass:";
                str+=pass;
                send(sock,str.c_str(),str.length(),0);//发送登录信息
                char buffer[1000];
                memset(buffer,0,sizeof(buffer));
                recv(sock,buffer,sizeof(buffer),0);//接收响应
                string recv_str(buffer);
                if(recv_str.substr(0,2)=="ok"){
                    if_login=true;
                    login_name=name;

                    //本地建立cookie文件保存sessionid
                    string tmpstr=recv_str.substr(2);
                    tmpstr="cat > cookie.txt <<end \n"+tmpstr+"\nend";
                    system(tmpstr.c_str());

                    cout<<"登陆成功\n\n";
                    break;
                }
                else
                    cout<<"密码或用户名错误！\n\n";
            }       
        }
    }
    //登陆成功
    while(if_login&&1){
        if(if_login){
            system("clear");
            cout<<"        欢迎回来,"<<login_name<<endl;
            cout<<" -------------------------------------------\n";
            cout<<"|                                           |\n";
            cout<<"|          请选择你要的选项：               |\n";
            cout<<"|              0:退出                       |\n";
            cout<<"|              1:发起单独聊天               |\n";
            cout<<"|              2:发起群聊                   |\n";
            cout<<"|                                           |\n";
            cout<<" ------------------------------------------- \n\n";
        }
        cin>>choice;
        if(choice==0)
            break;
        //私聊 
        else if(choice==1){
            cout<<"请输入对方的用户名:";
            string target_name,content;
            cin>>target_name;
            string sendstr("target:"+target_name+"from:"+login_name);//标识目标用户+源用户
            send(sock,sendstr.c_str(),sendstr.length(),0);//先向服务器发送目标用户、源用户
            cout<<"请输入你想说的话(输入exit退出)：\n";
            thread t1(client::SendMsg,conn); //创建发送线程
            thread t2(client::RecvMsg,conn);//创建接收线程
            t1.join();
            t2.join();
        } 
        //群聊
        else if(choice==2){
            cout<<"请输入群号:";
            int num;
            cin>>num;
            string sendstr("group:"+to_string(num));
            send(sock,sendstr.c_str(),sendstr.length(),0);
            cout<<"请输入你想说的话(输入exit退出)：\n";
            thread t1(client::SendMsg,-conn); //创建发送线程，传入负数，和私聊区分开
            thread t2(client::RecvMsg,conn);//创建接收线程
            t1.join();
            t2.join();
        }
    } 
    close(sock);
}
//注意，前面不用加static！
void client::SendMsg(int conn){
    while (1)
    {
        string str;
        cin>>str;
        //私聊消息
        if(conn>0){
            str="content:"+str;
        }
        //群聊信息
        else if(conn<0){
            str="gr_message:"+str;
        }   
        int ret=send(abs(conn), str.c_str(), str.length(),0); //发送
        //输入exit或者对端关闭时结束
        if(str=="content:exit"||ret<=0)
            break;
    }  
}
//注意，前面不用加static！
void client::RecvMsg(int conn){
    //接收缓冲区
    char buffer[1000];
    //不断接收数据
    while(1)
    {
        memset(buffer,0,sizeof(buffer));
        int len = recv(conn, buffer, sizeof(buffer),0);
        //recv返回值小于等于0，退出
        if(len<=0)
            break;
        cout<<buffer<<endl;
    }
}
