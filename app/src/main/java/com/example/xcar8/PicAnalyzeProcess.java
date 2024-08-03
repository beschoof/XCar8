package com.example.xcar8;
// stnd7.4.20, laptop

import android.util.Log;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;

import org.opencv.android.CameraBridgeViewBase;
import org.opencv.core.Core;
import org.opencv.core.Mat;
import org.opencv.core.MatOfPoint;
import org.opencv.core.Rect;
import org.opencv.core.Scalar;
import org.opencv.core.Size;
import org.opencv.imgproc.Imgproc;

import java.util.ArrayList;

class PicAnalyzeProcess {
   private static final String TAG = "### PicAnalyzeProcess: ";

   private ImageView imgView;
   private final Mat hsv = new Mat();
   private final Mat dst = new Mat();
   private final Mat intermediate = new Mat();
   private final Mat temp2 = new Mat();
   private final Mat hierarchy = new Mat();
   private final ArrayList<MatOfPoint> contours = new ArrayList<MatOfPoint>();
   private int direction = 0;
   private final int c0;
   private final int c1;
   MainActivity mainActivity;

   PicAnalyzeProcess(CameraBridgeViewBase anOpenCvCameraView, MainActivity mainActivity,
                     int x, int y) {
      Log.i(TAG, "PicAnalyzeProcess instantiated");
      this.mainActivity = mainActivity;

      c0 = x;  // FarbBereich
      c1 = y;
   }

   void run(Mat cameraPic) {
      Log.i(TAG, "PicAnalyzeProcess started");
         Mat img_result = processPic(cameraPic);
         Log.w(TAG, "Img_result: " + d2Str(direction));
   }

   void setCameraPic(Mat pic) {
   }

   private Mat processPic(@NonNull Mat src) {
      Imgproc.cvtColor(src, hsv, Imgproc.COLOR_RGB2HSV);
      // welchen Farbwert hat ein HSV-Punkt?
      double[] pt = hsv.get(src.rows() / 2, src.cols() / 2);
      StringBuilder s3 = new StringBuilder(" --> pt: ");
      for (double v : pt) s3.append(v).append(", ");
      Log.w(TAG, s3.toString());
      // --->  rot: 10, gelb: 30, blau: 115, Hopseball: 80

      //  Farbwert / Sättigung / Helligkeit
      // rot liegt bei 160..180 und 0..20, also 2mal..
      if (c0 < c1) {
         Core.inRange(hsv, new Scalar(c0, 10, 110), new Scalar(c1, 255, 255), intermediate);
      } else {
         Core.inRange(hsv, new Scalar(c0, 10, 110), new Scalar(180, 255, 255), intermediate);
         Core.inRange(hsv, new Scalar(0, 10, 110), new Scalar(c1, 255, 255), temp2);
         Core.bitwise_or(intermediate, temp2, intermediate);
      }

      Mat erode = Imgproc.getStructuringElement(Imgproc.MORPH_ERODE, new Size(9, 9));
      Imgproc.erode(intermediate, dst, erode);
      Mat dilate = Imgproc.getStructuringElement(Imgproc.MORPH_DILATE, new Size(9, 9));
      Imgproc.dilate(dst, intermediate, dilate);
      contours.clear();
      Imgproc.findContours(intermediate, contours, hierarchy, 0, 2);

      Log.w(TAG, "founded contours: " + contours.size());
//      returnPic = src;
      Mat returnPic = src;

      int largestContour = -1;
      double area = 0;
      for (int i = 0; i < contours.size(); i++) {
         double cArea = Imgproc.contourArea(contours.get(i));
         if (cArea > area) {
            area = cArea;
            largestContour = i;
         }
      }

      Rect r = null;
      if (largestContour > -1)
         r = Imgproc.boundingRect(contours.get(largestContour));

      direction = 0;
      if (r != null) {
         Imgproc.rectangle(returnPic, r.tl(), r.br(), new Scalar(255, 255, 255), 2);
         Log.w(TAG, "c/r: " + returnPic.cols() + " / " + returnPic.rows() + ", R: " + r.tl() + "/" + r.br() + ", C: " + r.x + ":" + r.width);
//         if ((r.y + r.height/2) < this.getHeight() / 2) {  orig book
         int c = r.x + r.width / 2;
         if (c > src.cols() * 2 / 3) { // Move right
            direction = 3;
            mainActivity.findDir(MainActivity.ACTION_RIGHT);
         } else if (c > src.cols() / 3) { // Move center
            direction = 2;
            mainActivity.findDir(MainActivity.ACTION_MIDDLE);
         } else { // left
            direction = 1;
            mainActivity.findDir(MainActivity.ACTION_LEFT);
         }
      } else {
         mainActivity.findDir(MainActivity.ACTION_STOP);
      }
      Log.i(TAG, " quit run()");
      return returnPic;
      }

private String d2Str(int d) {
         String r = "+++";
         switch (d) {
            case -1: r = "NIX DA"; break;
            case 0: r = "???"; break;
            case 1: r = "<< LINKS"; break;
            case 2: r = "<< Mitte >>"; break;
            case 3: r = "RECHTS >>"; break;
         }
         return r;
      }
}
