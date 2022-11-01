package com.cubrid.plcsql.transpiler.ast;

import com.cubrid.plcsql.transpiler.Misc;

import java.text.SimpleDateFormat;

import java.time.LocalTime;

public class ExprTime implements I_Expr {

    public final LocalTime time;

    public ExprTime(LocalTime time) {
        this.time = time;
    }

    @Override
    public String toJavaCode() {
        return String.format("new Time(%d, %d, %d)",
            time.getHour(),
            time.getMinute(),
            time.getSecond());
    }

    // --------------------------------------------------
    // Private
    // --------------------------------------------------
}
