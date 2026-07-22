# Redis-7.0.15

## 硬软件依赖

| 项目 | 规格/版本 |
| :---: | :---: |
|CPU|鲲鹏920/920B|
|操作系统|openEuler 22.03 SP4|
|GCC|开源GCC10.3.1|

## 构建流程

首先下载本仓库分支下的`sourcesBuild.sh`和`redisKraio.spec`两个文件。

### 配置构建目录和资源下载

执行`sh sourcesBuild.sh`,可自动构建标准化的 rpmbuild 目录结构。同时为 RPM 包构建准备所需的社区源码和优化补丁，请保证服务器外部网络访问。

### 构建RPM包

构建 RPM 包前，需先将 spec 文件移入标准目录下。执行下面命令构建 RPM 包。

```shell
cd /root/rpmbuild/SPECS
rpmbuild -ba redisKraio.spec --define  "dist .oe2203sp4"
```

### 安装RPM包

构建结果在`/root/rpmbuild/RPMS/aarch64/`。

执行 `rpm -ivh xxx.rpm`进行 RPM 包安装。

```shell
rpm -ivh redis-kraio-7.0.15-1.aarch64.rpm
```
