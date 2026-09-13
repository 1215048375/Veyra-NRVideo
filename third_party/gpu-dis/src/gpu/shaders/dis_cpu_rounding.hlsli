// Distribution note (2026-09-09): project-owned additions and modifications
// are licensed under Apache-2.0. Upstream portions retain their original
// licenses and attribution; see NOTICE, PROVENANCE.md and licenses/.
// OpenCV DIS/reference-derived portions retain Intel (2000-2008), Willow
// Garage (2009) and other upstream copyrights and BSD/Apache terms.
// See licenses/OPENCV_DIS_BSD_HEADER.txt and OPENCV_APACHE_LICENSE.txt.

// Diagnostic CPU round-to-nearest emulation. FP64 residual corrections preserve
// the CPU FP32 operator contract; they are not lower-precision approximations.
// Product performance and hardware support must be measured before promotion.
float cpu_divide(float a, float b)
{
    precise float estimate = a / b;
    precise double residual = (double)a - (double)estimate * (double)b;
    precise double corrected = (double)estimate + residual / (double)b;
    return (float)corrected;
}
float cpu_sqrt(float a)
{
    precise float estimate = sqrt(a);
    precise double residual = (double)a - (double)estimate * (double)estimate;
    precise double corrected = (double)estimate + residual / (2.0 * (double)estimate);
    return (float)corrected;
}
