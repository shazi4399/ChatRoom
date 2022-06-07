#include "server.h"

vector<bool> server::sock_arr(10000,false);
unordered_map<string,int> server::name_sock_map;//名字和套接字描述符
unordered_map<string,string> server::from_to_map;//记录用户xx要向用户yy发送信息
unordered_map<int,set<int> > server::group_map;//记录群号和套接字描述符集合
pthread_mutex_t server::name_sock_mutx;//互斥锁，锁住需要修改name_sock_map的临界区
pthread_mutex_t server::group_mutx;//互斥锁，锁住需要修改group_map的临界区
pthread_mutex_t server::from_mutex;//自旋锁，锁住修改from_to_map的临界区

server::server(int port,string ip):server_port(port),server_ip(ip){
    pthread_mutex_init(&name_sock_mutx, NULL); //创建互斥锁
    pthread_mutex_init(&group_mutx, NULL); //创建互斥锁
    pthread_mutex_init(&from_mutex, NULL); //创建互斥锁
}
server::~server(){
    for(int i=0;i<sock_arr.size();i++){
        if(sock_arr[i])
            close(i);
    }
    close(server_sockfd);
}
//将参数的文件描述符设为非阻塞
void server::setnonblocking(int sock)  
{  
    int opts;  
    opts=fcntl(sock,F_GETFL);  
    if(opts<0)  
    {  
        perror("fcntl(sock,GETFL)");  
        exit(1);  
    }  
    opts = opts|O_NONBLOCK;  
    if(fcntl(sock,F_SETFL,opts)<0)  
    {  
        perror("fcntl(sock,SETFL,opts)");  
        exit(1);  
    }      
}  
void server::run(){
    //listen的backlog大小
    int LISTENQ=200;
    int i, maxi, listenfd, connfd, sockfd,epfd,nfds;  
    ssize_t n;  
    //char line[MAXLINE];  
    socklen_t clilen;  
    //声明epoll_event结构体的变量,ev用于注册事件,数组用于回传要处理的事件  
    struct epoll_event ev,events[10000];  
    //生成用于处理accept的epoll专用的文件描述符  
    epfd=epoll_create(10000);
    struct sockaddr_in clientaddr;  
    struct sockaddr_in serveraddr;  
    listenfd = socket(PF_INET, SOCK_STREAM, 0);  
    //把socket设置为非阻塞方式  
    setnonblocking(listenfd);  
    //设置与要处理的事件相关的文件描述符  
    ev.data.fd=listenfd;  
    //设置要处理的事件类型  
    ev.events=EPOLLIN|EPOLLET;  
    //注册epoll事件  
    epoll_ctl(epfd,EPOLL_CTL_ADD,listenfd,&ev); 
    //设置serveraddr
    bzero(&serveraddr, sizeof(serveraddr));  
    serveraddr.sin_family = AF_INET;  
    serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");//此处设为服务器的ip
    serveraddr.sin_port=htons(8023);  
    bind(listenfd,(sockaddr *)&serveraddr, sizeof(serveraddr));  
    listen(listenfd, LISTENQ); 
    clilen=sizeof(clientaddr);
    maxi = 0;   

    /* 定义一个10线程的线程池 */
    boost::asio::thread_pool tp(10);

    while(1){  
        cout<<"--------------------------"<<endl;
        cout<<"epoll_wait阻塞中"<<endl;
        //等待epoll事件的发生  
        nfds=epoll_wait(epfd,events,10000,-1);//最后一个参数是timeout，0:立即返回，-1:一直阻塞直到有事件，x:等待x毫秒
        cout<<"epoll_wait返回，有事件发生"<<endl;
        //处理所发生的所有事件  
        for(i=0;i<nfds;++i)  
        {  
            //有新客户端连接服务器
            if(events[i].data.fd==listenfd) 
            {  
                connfd = accept(listenfd,(sockaddr *)&clientaddr, &clilen);  
                if(connfd<0){  
                     perror("connfd<0");  
                     exit(1);  
                }  
                else{
                    cout<<"用户"<<inet_ntoa(clientaddr.sin_addr)<<"正在连接\n";
                }
                //设置用于读操作的文件描述符  
                ev.data.fd=connfd;  
                //设置用于注册的读操作事件，采用ET边缘触发，为防止多个线程处理同一socket而使用EPOLLONESHOT  
                ev.events=EPOLLIN|EPOLLET|EPOLLONESHOT;
                //边缘触发要将套接字设为非阻塞
                setnonblocking(connfd);
                //注册ev  
                epoll_ctl(epfd,EPOLL_CTL_ADD,connfd,&ev);  
            }  
            //接收到读事件
            else if(events[i].events&EPOLLIN)  
            {  
                sockfd = events[i].data.fd;
                events[i].data.fd=-1;
                cout<<"接收到读事件"<<endl;

                string recv_str;
                boost::asio::post(boost::bind(RecvMsg,epfd,sockfd)); //加入任务队列，处理事件
            }  
        } 
    }  
    close(listenfd);
}
//注意，前面不用加static！
void server::RecvMsg(int epollfd,int conn){
    tuple<bool,string,string,int,int> info;//元组类型，四个成员分别为if_login、login_name、target_name、target_conn
    /*
        bool if_login;//记录当前服务对象是否成功登录
        string login_name;//记录当前服务对象的名字
        string target_name;//记录目标对象的名字
        int target_conn;//目标对象的套接字描述符
        int group_num;//记录所处群号
    */
    get<0>(info)=false;
    get<3>(info)=-1;

    string recv_str;
    while(1){
        char buf[10];
        memset(buf, 0, sizeof(buf));
        int ret  = recv(conn, buf, sizeof(buf), 0);
        if(ret < 0){
            cout<<"recv返回值小于0"<<endl;
            //对于非阻塞IO，下面的事件成立标识数据已经全部读取完毕
            if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                    printf("数据读取完毕\n");
                cout<<"接收到的完整内容为："<<recv_str<<endl;
                cout<<"开始处理事件"<<endl;
                break;
            }
            cout<<"errno:"<<errno<<endl;
            return;
        }
        else if(ret == 0){
            cout<<"recv返回值为0"<<endl;
            return;
        }
        else{
            printf("接收到内容如下: %s\n",buf);
            string tmp(buf);
            recv_str+=tmp;
        }
    }
    string str=recv_str;
    HandleRequest(epollfd,conn,str,info);
}
void server::HandleRequest(int epollfd,int conn,string str,tuple<bool,string,string,int,int> &info){
    char buffer[1000];
    string name,pass;
    //把参数提出来，方便操作
    bool if_login=get<0>(info);//记录当前服务对象是否成功登录
    string login_name=get<1>(info);//记录当前服务对象的名字
    string target_name=get<2>(info);//记录目标对象的名字
    int target_conn=get<3>(info);//目标对象的套接字描述符
    int group_num=get<4>(info);//记录所处群号

    //连接MYSQL数据库
    MYSQL *con=mysql_init(NULL);
    mysql_real_connect(con,"127.0.0.1","root","","ChatProject",0,NULL,CLIENT_MULTI_STATEMENTS);

    //连接redis数据库
    redisContext *redis_target = redisConnect("127.0.0.1",6379);
    if(redis_target->err){
        redisFree(redis_target);
        cout<<"连接redis失败"<<endl;     
    }

    //先接收cookie看看redis是否保存该用户的登录状态
    if(str.find("cookie:")!=str.npos){
        cout<<"cookie方法\n";
        string cookie=str.substr(7);
        // 查询该cookie是否存在：hget cookie name
        string redis_str="hget "+cookie+" name";
        redisReply *r = (redisReply*)redisCommand(redis_target,redis_str.c_str());
        string send_res;
        //存在
        if(r->str){
            cout<<"查询redis结果："<<r->str<<endl;
            send_res=r->str;
        }
        //不存在
        else
            send_res="NULL";
        send(conn,send_res.c_str(),send_res.length()+1,0);
    }

    //注册
    else if(str.find("name:")!=str.npos){
        cout<<"注册方法\n";
        int p1=str.find("name:"),p2=str.find("pass:");
        name=str.substr(p1+5,p2-5);
        pass=str.substr(p2+5,str.length()-p2-4);
        string search="INSERT INTO USER VALUES (\"";
        search+=name;
        search+="\",\"";
        search+=pass;
        search+="\");";
        cout<<"sql语句:"<<search<<endl<<endl;
        mysql_query(con,search.c_str());
    } 

    //登录
    else if(str.find("login")!=str.npos){
        cout<<"登录方法\n";
        int p1=str.find("login"),p2=str.find("pass:");
        name=str.substr(p1+5,p2-5);
        pass=str.substr(p2+5,str.length()-p2-4);
        string search="SELECT * FROM USER WHERE NAME=\"";
        search+=name;
        search+="\";";
        cout<<"sql语句:"<<search<<endl;
        auto search_res=mysql_query(con,search.c_str());
        auto result=mysql_store_result(con);
        int col=mysql_num_fields(result);//获取列数
        int row=mysql_num_rows(result);//获取行数
        //查询到用户名
        if(search_res==0&&row!=0){
            cout<<"查询成功\n";
            auto info=mysql_fetch_row(result);//获取一行的信息
            cout<<"查询到用户名:"<<info[0]<<" 密码:"<<info[1]<<endl;
            //密码正确
            if(info[1]==pass){
                cout<<"登录密码正确\n\n";
                string str1="ok";
                if_login=true;
                login_name=name;
                pthread_mutex_lock(&name_sock_mutx); //上锁
                name_sock_map[login_name]=conn;//记录下名字和文件描述符的对应关系
                pthread_mutex_unlock(&name_sock_mutx); //解锁
                
                // 随机生成sessionid并发送到客户端
                srand(time(NULL));//初始化随机数种子
                for(int i=0;i<10;i++){
                    int type=rand()%3;//type为0代表数字，为1代表小写字母，为2代表大写字母
                    if(type==0)
                        str1+='0'+rand()%9;
                    else if(type==1)
                        str1+='a'+rand()%26;
                    else if(type==2)
                        str1+='A'+rand()%26;
                }
                //将sessionid存入redis
                string redis_str="hset "+str1.substr(2)+" name "+login_name;
                redisReply *r = (redisReply*)redisCommand(redis_target,redis_str.c_str());
                //设置生存时间,默认300秒
                redis_str="expire "+str1.substr(2)+" 300";
                r=(redisReply*)redisCommand(redis_target,redis_str.c_str());
                cout<<"随机生成的sessionid为："<<str1.substr(2)<<endl;

                send(conn,str1.c_str(),str1.length()+1,0);
            }
            //密码错误
            else{
                cout<<"登录密码错误\n\n";
                char str1[100]="wrong";
                send(conn,str1,strlen(str1),0);
            }   
        }
        //没找到用户名
        else{
            cout<<"查询失败\n\n";
            char str1[100]="wrong";
            send(conn,str1,strlen(str1),0);
        }
    }

    //设定目标的文件描述符
    else if(str.find("target:")!=str.npos){
        cout<<"设定目标方法\n";
        int pos1=str.find("from");
        string target=str.substr(7,pos1-7),from=str.substr(pos1+5);
        target_name=target;
        if(name_sock_map.find(target)==name_sock_map.end()){
            cout<<"源用户为"<<login_name<<",目标用户"<<target_name<<"仍未登陆，无法发起私聊\n";
        }
        else{
            pthread_mutex_lock(&from_mutex);
            from_to_map[from]=target;
            cout<<"from:"<<from<<"  target:"<<target<<endl;
            pthread_mutex_unlock(&from_mutex);
            login_name=from;
            cout<<"源用户"<<login_name<<"向目标用户"<<target_name<<"发起的私聊即将建立";
            cout<<",目标用户的套接字描述符为"<<name_sock_map[target]<<endl;
            target_conn=name_sock_map[target];
        }       
    }

    //接收到消息，转发
    else if(str.find("content:")!=str.npos){
        target_conn=-1;
        cout<<"转发方法\n";
         //根据两个map找出当前用户和目标用户
        for(auto i:name_sock_map){
            if(i.second==conn){
                login_name=i.first;
                target_name=from_to_map[i.first];
                target_conn=name_sock_map[target_name];
                break;
            }       
        }
        if(target_conn==-1){
            cout<<"找不到目标用户"<<target_name<<"的套接字，将尝试重新寻找目标用户的套接字\n";
            if(name_sock_map.find(target_name)!=name_sock_map.end()){
                target_conn=name_sock_map[target_name];
                cout<<"重新查找目标用户套接字成功\n";
            }
            else{
                cout<<"查找仍然失败，转发失败！\n";
            }
        }
        string recv_str(str);
        string send_str=recv_str.substr(8);
        cout<<"用户"<<login_name<<"向"<<target_name<<"发送:"<<send_str<<endl;
        send_str="["+login_name+"]:"+send_str;
        send(target_conn,send_str.c_str(),send_str.length(),0);
    }

     //绑定群聊号
    else if(str.find("group:")!=str.npos){
        cout<<"绑定群聊号方法\n";
        string recv_str(str);
        string num_str=recv_str.substr(6);
        group_num=stoi(num_str);
        //找出当前用户
        for(auto i:name_sock_map)
            if(i.second==conn){
                login_name=i.first;
                break;
            }
        cout<<"用户"<<login_name<<"绑定群聊号为："<<num_str<<endl;
        pthread_mutex_lock(&group_mutx);//上锁
        group_map[group_num].insert(conn);
        pthread_mutex_unlock(&group_mutx);//解锁
    }

    //广播群聊信息
    else if(str.find("gr_message:")!=str.npos){
        cout<<"广播群聊信息方法\n";
         //找出当前用户
        for(auto i:name_sock_map)
            if(i.second==conn){
                login_name=i.first;
                break;
            }
        //找出群号
        for(auto i:group_map)
            if(i.second.find(conn)!=i.second.end()){
                group_num=i.first;
                break;
            }
        string send_str(str);
        send_str=send_str.substr(11);
        send_str="["+login_name+"]:"+send_str;
        cout<<"群聊信息："<<send_str<<endl;
        for(auto i:group_map[group_num]){
            if(i!=conn)
                send(i,send_str.c_str(),send_str.length(),0);
        }       
    }

    //线程工作完毕后重新注册事件
    epoll_event event;
    event.data.fd=conn;
    event.events=EPOLLIN|EPOLLET|EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,conn,&event);

    mysql_close(con);
    if(!redis_target->err)
        redisFree(redis_target);

    //更新实参
    get<0>(info)=if_login;//记录当前服务对象是否成功登录
    get<1>(info)=login_name;//记录当前服务对象的名字
    get<2>(info)=target_name;//记录目标对象的名字
    get<3>(info)=target_conn;//目标对象的套接字描述符
    get<4>(info)=group_num;//记录所处群号
}