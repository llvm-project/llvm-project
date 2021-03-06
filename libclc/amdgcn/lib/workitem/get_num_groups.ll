declare i32 @llvm.r600.read.ngroups.x() nounwind readnone
declare i32 @llvm.r600.read.ngroups.y() nounwind readnone
declare i32 @llvm.r600.read.ngroups.z() nounwind readnone

define i64 @get_num_groups(i32 %dim) nounwind readnone alwaysinline {
  switch i32 %dim, label %default [i32 0, label %x_dim i32 1, label %y_dim i32 2, label %z_dim]
x_dim:
  %x = call i32 @llvm.r600.read.ngroups.x()
  %x.ext = zext i32 %x to i64
  ret i64 %x.ext
y_dim:
  %y = call i32 @llvm.r600.read.ngroups.y()
  %y.ext = zext i32 %y to i64
  ret i64 %y.ext
z_dim:
  %z = call i32 @llvm.r600.read.ngroups.z()
  %z.ext = zext i32 %z to i64
  ret i64 %z.ext
default:
  ret i64 1
}
