package com.realsenseai.rsid.sample.util;


import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.widget.TextView;
import com.google.android.material.snackbar.Snackbar;

public class SnackbarHelper {

  public static void showSnackbarMessage(View view, String message, Context context) {
    Snackbar snackbar = Snackbar.make(view, message, Snackbar.LENGTH_LONG);
    View snackBarView = snackbar.getView();
    TextView textView = snackBarView.findViewById(com.google.android.material.R.id.snackbar_text);
    textView.setGravity(Gravity.CENTER);
    textView.setTextAlignment(View.TEXT_ALIGNMENT_CENTER);

    //snackbar.setTextColor(getResources().getColor(com.google.android.material.R.color.foreground_material_dark);
    //snackbar.setBackgroundTint(ContextCompat.getColor(context, com.google.android.material.R.color.cardview_light_background));
    //View snackBarView = snackbar.getView();
    //FrameLayout.LayoutParams params = (FrameLayout.LayoutParams)snackBarView.getLayoutParams();
    //params.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
    //snackBarView.setLayoutParams(params);
    snackbar.show();
  }

  //public static void showSuccess(Context context, String message) {
  //  Snackbar.make(view, message, Snackbar.LENGTH_SHORT).show();
  //}
  //
  //public static void showInfo(Context context, String message) {
  //  Snackbar.make(view, message, Snackbar.LENGTH_SHORT).show();
  //}
}