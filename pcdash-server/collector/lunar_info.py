"""农历完整信息计算 — 基于 sxtwl，支持干支年月日 + 节气"""

from datetime import date
import sxtwl

_GAN = ["甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"]
_ZHI = ["子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"]
_YMC = ["正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"]
_RMC = [
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
]
# sxtwl 节气索引顺序（从小寒起）
_JQC = [
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
]


def _gz(gz_obj) -> str:
    return _GAN[gz_obj.tg] + _ZHI[gz_obj.dz]


def get_lunar(solar_date: date | None = None) -> dict | None:
    """返回完整农历信息，失败时返回 None。

    字段说明：
      gz_y  干支年，如 "丙午年"
      gz_m  干支月，如 "壬午月"
      gz_d  干支日，如 "甲子日"
      mo    农历月，如 "四月" / "闰四月"
      dy    农历日，如 "廿五"
      jq    节气名，如 "芒种"；非节气日为空字符串 ""
    """
    if solar_date is None:
        solar_date = date.today()
    try:
        d = sxtwl.fromSolar(solar_date.year, solar_date.month, solar_date.day)

        lm = d.getLunarMonth()
        ld = d.getLunarDay()
        leap = d.isLunarLeap()

        jq_idx = d.getJieQi()
        jq_str = _JQC[jq_idx] if 0 <= jq_idx < len(_JQC) else ""

        return {
            "gz_y": _gz(d.getYearGZ()) + "年",
            "gz_m": _gz(d.getMonthGZ()) + "月",
            "gz_d": _gz(d.getDayGZ()) + "日",
            "mo":   ("闰" if leap else "") + _YMC[lm - 1] + "月",
            "dy":   _RMC[ld - 1],
            "jq":   jq_str,
        }
    except Exception:
        return None
