特征和用法:
> 基于unix/linux系统的开发
> URL排重
> 文档压缩存储
> 支持多下载节点分布式下载
> 可通过http://127.0.0.1:3721/ 查看下载情况统计,下载任务控制(可停止和恢复任务)
> 依赖基本通信库libevbase 和 libsbase (安装的时候需要先安装这个两个库)

安装和使用
> 普通模式 : ./configure  && make && make install
> 调试模式(可查看更多的详细log) : ./configure --enable-debug && make && make install
> > hispiderd为中心控制节点程序 配置文件为doc/rc.hispiderd.ini  配置文件里有一个basedir 所有的数据存储在这个目录下,hispider.doc为文档存储文件
> > hispider为下载节点程序 配置文件为doc/rc.hispider.ini 如果两个节点不在一台机器上需要配置server\_ip 默认端口为3721

> 运行
> ./hispiderd -c ../doc/rc.hispiderd.ini
> ./hispider -c ../doc/rc.hispider.ini

文档存储格式:
> typedef struct _LHEADER
> {
> > int ndate;
> > int nurl;
> > int nzdata;
> > int ndata;

> }LHEADER;
顺序存储文档的头,url,文档压缩正文, 采用zlib压缩 ndata为正常长度, nzdata为压缩后也就是存储的实际长度. nurl为url长度实际存储为url\0 长度为nurl+1_


下载地址
> hispider http://hispider.googlecode.com/files/hispider-0.1.0.tar.gz
> libevbase http://sbase.googlecode.com/files/libevbase-0.0.14.tar.gz
> libsbase http://sbase.googlecode.com/files/libsbase-0.3.1.tar.gz