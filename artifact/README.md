# Artifact Evaluation

> [!NOTE]
> All operations are performed in the root directory of the repository.

## (Step 1) Build

You should use the following command to build the base image, all evaluation is based on the image.

```bash
docker compose up --build shadowbound
```

## (Step 2) Basic Test

Our basic test include use the ShadowBound to compile nginx and run it, you can use the following command to achieve it.

```bash
docker compose up --build nginx-eval
```

After running the command, you can access the result at `eval/nginx/results/shadowbound.txt`, the results should be like this:

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

### (Step 3) Integration Test

To show ShadowBound can cooperate with other UAF defenses, we show how we use ShadowBound+FFMalloc and ShadowBound+MarkUS to compile nginx and run it.

> [!WARNING]
> You may encounter a `Segmentation fault` during the test. It is due to UAF defense issues, not ShadowBound. If this occurs, try running the test again. In our experience, such issues are relatively rare.


```bash
TARGET=shadowbound-ffmalloc docker compose up --build nginx-eval
TARGET=shadowbound-markus docker compose up --build nginx-eval
```

After running the command, you can access the result at `eval/nginx/results/shadowbound-ffmalloc.txt` and `eval/nginx/results/shadowbound-markus.txt`.

### (Step 4) Security Test

In the evaluation, we show how ShadowBound can prevent real world vulnerabilities. You can choose use our pre-built image to run the test or you can also build the image by yourself.

#### Use Pre-built Image

```bash
docker pull dataisland/shadowbound-seceval:latest
docker run -it dataisland/shadowbound-seceval /bin/bash
/root/test.sh
```

#### Build Image by Yourself

> [!WARNING]
> Please ensure that your network and machine is stable, the building process may take a long time and download a lot of data. 
> If you encounter any problems, please use our pre-built image as a safe choice.

First you should build the image by this command:

```bash
docker compose up --build sec-eval
```

Then you can enter the container, build all the test cases and run them.

```bash
docker run -it sec-eval /bin/bash
/root/build.sh && /root/test.sh
```


### (Step 5) Performance Test
