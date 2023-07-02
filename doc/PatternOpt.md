# Identify the Pointer and Length Pattern of Structure

## Example

### pattern.cpp

```llvm
define dso_local void @_Z6createP6Struct(%class.Struct* noundef %0) #4 {
  %2 = alloca %class.Struct*, align 8
  store %class.Struct* %0, %class.Struct** %2, align 8
  %3 = load %class.Struct*, %class.Struct** %2, align 8
  %4 = getelementptr inbounds %class.Struct, %class.Struct* %3, i32 0, i32 1
  %5 = load i32, i32* %4, align 8
  %6 = sext i32 %5 to i64
  %7 = call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %6, i64 24)
  %8 = extractvalue { i64, i1 } %7, 1
  %9 = extractvalue { i64, i1 } %7, 0
  %10 = select i1 %8, i64 -1, i64 %9
  %11 = call noalias noundef nonnull i8* @_Znam(i64 noundef %10) #10
  %12 = bitcast i8* %11 to %struct.Test*
  %13 = icmp eq i64 %6, 0
  br i1 %13, label %20, label %14

14:                                               ; preds = %1
  %15 = getelementptr inbounds %struct.Test, %struct.Test* %12, i64 %6
  br label %16

16:                                               ; preds = %16, %14
  %17 = phi %struct.Test* [ %12, %14 ], [ %18, %16 ]
  call void @_ZN4TestC2Ev(%struct.Test* noundef nonnull align 8 dereferenceable(20) %17) #3
  %18 = getelementptr inbounds %struct.Test, %struct.Test* %17, i64 1
  %19 = icmp eq %struct.Test* %18, %15
  br i1 %19, label %20, label %16

20:                                               ; preds = %1, %16
  %21 = load %class.Struct*, %class.Struct** %2, align 8
  %22 = getelementptr inbounds %class.Struct, %class.Struct* %21, i32 0, i32 0
  store %struct.Test* %12, %struct.Test** %22, align 8
  ret void
}
◊
```

### 444.named

```llvm
18:
  %19 = getelementptr inbounds %class.ComputeList, %class.ComputeList* %0, i64 0, i32 1
  %20 = load i32, i32* %19, align 4
  %21 = sext i32 %20 to i64
  %22 = call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %21, i64 24)
  %23 = extractvalue { i64, i1 } %22, 1
  %24 = extractvalue { i64, i1 } %22, 0
  %25 = call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %24, i64 8)
  %26 = extractvalue { i64, i1 } %25, 1
  %27 = or i1 %23, %26
  %28 = extractvalue { i64, i1 } %25, 0
  %29 = select i1 %27, i64 -1, i64 %28
  %30 = call noalias noundef nonnull i8* @_Znam(i64 noundef %29) #14
  %31 = bitcast i8* %30 to i64*
  store i64 %21, i64* %31, align 16
  %32 = getelementptr inbounds i8, i8* %30, i64 8
  %33 = icmp eq i32 %20, 0
  br i1 %33, label %42, label %34

34:
  %35 = bitcast i8* %32 to %class.PairCompute*
  %36 = getelementptr inbounds %class.PairCompute, %class.PairCompute* %35, i64 %21
  br label %37

37:
  %38 = phi %class.PairCompute* [ %35, %34 ], [ %40, %37 ]
  call void @llvm.dbg.value(metadata %class.PairCompute* %38, metadata !450, metadata !DIExpression())
  %39 = getelementptr inbounds %class.PairCompute, %class.PairCompute* %38, i64 0, i32 0, i32 0
  store i32 (...)** bitcast (i8** getelementptr inbounds ({ [4 x i8*] }, { [4 x i8*] }* @_ZTV11PairCompute, i64 0, inrange i32 0, i64 2) to i32 (...)**), i32 (...)*** %39, align 8
  %40 = getelementptr inbounds %class.PairCompute, %class.PairCompute* %38, i64 1
  %41 = icmp eq %class.PairCompute* %40, %36
  br i1 %41, label %42, label %37

42:
  %43 = getelementptr inbounds %class.ComputeList, %class.ComputeList* %0, i64 0, i32 3
  %44 = bitcast %class.PairCompute** %43 to i8**
  store i8* %32, i8** %44, align 8
```