package com.stm32.envmonitor.ui.components

import androidx.annotation.DrawableRes
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.stm32.envmonitor.ui.theme.BorderMuted
import com.stm32.envmonitor.ui.theme.SurfaceWhite
import com.stm32.envmonitor.ui.theme.TextPrimary
import com.stm32.envmonitor.ui.theme.TextSecondary

@Composable
fun AppTopBar(
    title: String,
    subtitle: String,
    leading: (@Composable () -> Unit)? = null,
    trailing: (@Composable () -> Unit)? = null,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        leading?.invoke()
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                color = TextPrimary,
                fontSize = 28.sp,
                fontWeight = FontWeight.Bold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = subtitle,
                color = TextSecondary,
                fontSize = 13.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        trailing?.invoke()
    }
}

@Composable
fun CircleIconButton(
    @DrawableRes iconRes: Int,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    iconTint: Color = Color.Unspecified,
    containerColor: Color = SurfaceWhite,
    enabled: Boolean = true,
) {
    Box(
        modifier = modifier
            .size(48.dp)
            .clip(CircleShape)
            .background(containerColor)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        AppIcon(
            painter = painterResource(iconRes),
            contentDescription = contentDescription,
            tint = iconTint,
            size = 22.dp,
        )
    }
}

@Composable
fun AppIcon(
    painter: Painter,
    contentDescription: String?,
    tint: Color = Color.Unspecified,
    size: androidx.compose.ui.unit.Dp,
) {
    androidx.compose.material3.Icon(
        painter = painter,
        contentDescription = contentDescription,
        tint = tint,
        modifier = Modifier.size(size),
    )
}

@Composable
fun CardSurface(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(22.dp),
        colors = CardDefaults.cardColors(containerColor = SurfaceWhite),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp),
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            content = content,
        )
    }
}

@Composable
fun SectionTitle(title: String, modifier: Modifier = Modifier) {
    Text(
        text = title,
        modifier = modifier,
        color = TextPrimary,
        fontSize = 20.sp,
        fontWeight = FontWeight.Bold,
    )
}

@Composable
fun InfoBanner(
    text: String,
    modifier: Modifier = Modifier,
    textColor: Color,
    containerColor: Color,
) {
    Text(
        text = text,
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(containerColor)
            .padding(horizontal = 18.dp, vertical = 14.dp),
        color = textColor,
        fontSize = 14.sp,
        lineHeight = 20.sp,
    )
}

@Composable
fun OutlinedChip(
    label: String,
    modifier: Modifier = Modifier,
    backgroundColor: Color = SurfaceWhite,
) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(18.dp))
            .background(backgroundColor)
            .border(1.dp, BorderMuted, RoundedCornerShape(18.dp))
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(
            text = label,
            color = TextSecondary,
            fontSize = 13.sp,
        )
    }
}
