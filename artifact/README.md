# 📜 Artifact Evaluation

> [!NOTE]
> All operations are performed in the root directory of the repository.

## 🛠️ (Step 1) Build

You should use the following command to build the base image, all evaluation is based on the image.

```bash
docker compose build shadowbound
```

## 🧪 (Step 2) Basic Test

Our basic test includes using ShadowBound to compile Nginx and run it. You can use the following command to achieve it:

```bash
docker compose up nginx-eval
```

After running the command, you can access the result at `artifact/nginx/results/shadowbound.txt`. The results should look like this:

```
Running 1m test @ http://localhost:80/index.html
  8 threads and 100 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   816.19us  418.18us  50.10ms   99.18%
    Req/Sec    14.96k   717.36    30.03k    92.40%
  Latency Distribution
     50%  786.00us
     75%  805.00us
     90%    0.85ms
     99%    1.15ms
  7149133 requests in 1.00m, 5.68GB read
Requests/sec: 118955.36
Transfer/sec:     96.77MB
```

### 🔗 (Step 3) Integration Test

To show ShadowBound can cooperate with other UAF defenses, we demonstrate how to use ShadowBound+FFMalloc and ShadowBound+MarkUS to compile Nginx and run it.

> [!WARNING]
> You may encounter a `Segmentation fault` during the test. This is due to UAF defense issues, not ShadowBound. If this occurs, try running the test again. In our experience, such issues are relatively rare.

```bash
TARGET=shadowbound-ffmalloc docker compose up nginx-eval
TARGET=shadowbound-markus docker compose up nginx-eval
```

After running the command, you can access the result at `artifact/nginx/results/shadowbound-ffmalloc.txt` and `artifact/nginx/results/shadowbound-markus.txt`.

### 🔐 (Step 4) Security Test

In the evaluation, we demonstrate how ShadowBound can prevent real-world vulnerabilities. You can choose to use our pre-built image to run the test or build the image yourself.

#### 🖼️ Use Pre-built Image

```bash
docker pull ghcr.io/cla7aye15i4nd/shadowbound/shadowbound-sec-eval:1.0.0
docker run -it ghcr.io/cla7aye15i4nd/shadowbound/shadowbound-sec-eval:1.0.0 /root/test.sh
```

After running the command, you should see the following output:

```shell
[+] 2017-9164-9166
[+] 2017-9167-9173
[+] CVE-2006-6563
[+] CVE-2009-2285
[+] CVE-2013-4243
[+] CVE-2013-7443
[+] CVE-2014-1912
[+] CVE-2015-8668
[+] CVE-2015-9101
[+] CVE-2016-10270
[+] CVE-2016-10271
...
```

#### 🔨 Build Image by Yourself

> [!WARNING]
> Please ensure that your network and machine are stable. The building process may take a long time and download a lot of data. 
> If you encounter any problems, please use our pre-built image as a safe choice.

First, build the image and run the test:

```bash
docker compose run sec-eval /root/test.sh
```

### ⚡ (Step 5) SPEC CPU2017 Performance Test

In this step, we demonstrate the performance of ShadowBound. You can use the following command to run the test:

#### 🖼️ Use Pre-built Image

```bash
docker pull ghcr.io/cla7aye15i4nd/shadowbound/shadowbound-spec2017-eval:1.0.0
docker run --privileged -it ghcr.io/cla7aye15i4nd/shadowbound/shadowbound-spec2017-eval:1.0.0 /bin/bash
python3 /root/scripts/spectest.py | tee /root/spectest.log
```

After running the command, you can the result at `/root/spectest.log`.

#### 🔨 Build Image by Yoursel

If you want to build the image by yourself, you need to download the SPEC CPU 2017 benchmark suite (`cpu2017.iso`) from the official website. Then, use the following command to build the image:

```bash
CPU2017_PATH=/path/to/cpu2017.iso artifact/spec2017/build.sh
```

### ⚡ (Step 6) Real World Performance Test

In this step, we demonstrate the performance of ShadowBound in real-world application, include Nginx and ChakraCore. You can use the following command to run the test:

#### Nginx

```bash
./artifact/nginx/test.sh 
```

#### ChakraCore

