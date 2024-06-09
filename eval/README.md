# Artifact Evaluated

All operations are performed in the root directory of the repository.

## Build

You should use the following command to build the base image, all evaluation is based on the image.

```bash
docker compose up --build shadowbound
```

## Basic Test

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
